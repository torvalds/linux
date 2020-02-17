/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/blk-mq-pci.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

#include "qla_target.h"

/*
 * Driver version
 */
char qla2x00_version_str[40];

static int apidev_major;

/*
 * SRB allocation cache
 */
struct kmem_cache *srb_cachep;

/*
 * CT6 CTX allocation cache
 */
static struct kmem_cache *ctx_cachep;
/*
 * error level for logging
 */
int ql_errlev = ql_log_all;

static int ql2xenableclass2;
module_param(ql2xenableclass2, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xenableclass2,
		"Specify if Class 2 operations are supported from the very "
		"beginning. Default is 0 - class 2 not supported.");


int ql2xlogintimeout = 20;
module_param(ql2xlogintimeout, int, S_IRUGO);
MODULE_PARM_DESC(ql2xlogintimeout,
		"Login timeout value in seconds.");

int qlport_down_retry;
module_param(qlport_down_retry, int, S_IRUGO);
MODULE_PARM_DESC(qlport_down_retry,
		"Maximum number of command retries to a port that returns "
		"a PORT-DOWN status.");

int ql2xplogiabsentdevice;
module_param(ql2xplogiabsentdevice, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xplogiabsentdevice,
		"Option to enable PLOGI to devices that are not present after "
		"a Fabric scan.  This is needed for several broken switches. "
		"Default is 0 - no PLOGI. 1 - perfom PLOGI.");

int ql2xloginretrycount = 0;
module_param(ql2xloginretrycount, int, S_IRUGO);
MODULE_PARM_DESC(ql2xloginretrycount,
		"Specify an alternate value for the NVRAM login retry count.");

int ql2xallocfwdump = 1;
module_param(ql2xallocfwdump, int, S_IRUGO);
MODULE_PARM_DESC(ql2xallocfwdump,
		"Option to enable allocation of memory for a firmware dump "
		"during HBA initialization.  Memory allocation requirements "
		"vary by ISP type.  Default is 1 - allocate memory.");

int ql2xextended_error_logging;
module_param(ql2xextended_error_logging, int, S_IRUGO|S_IWUSR);
module_param_named(logging, ql2xextended_error_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xextended_error_logging,
		"Option to enable extended error logging,\n"
		"\t\tDefault is 0 - no logging.  0x40000000 - Module Init & Probe.\n"
		"\t\t0x20000000 - Mailbox Cmnds. 0x10000000 - Device Discovery.\n"
		"\t\t0x08000000 - IO tracing.    0x04000000 - DPC Thread.\n"
		"\t\t0x02000000 - Async events.  0x01000000 - Timer routines.\n"
		"\t\t0x00800000 - User space.    0x00400000 - Task Management.\n"
		"\t\t0x00200000 - AER/EEH.       0x00100000 - Multi Q.\n"
		"\t\t0x00080000 - P3P Specific.  0x00040000 - Virtual Port.\n"
		"\t\t0x00020000 - Buffer Dump.   0x00010000 - Misc.\n"
		"\t\t0x00008000 - Verbose.       0x00004000 - Target.\n"
		"\t\t0x00002000 - Target Mgmt.   0x00001000 - Target TMF.\n"
		"\t\t0x7fffffff - For enabling all logs, can be too many logs.\n"
		"\t\t0x1e400000 - Preferred value for capturing essential "
		"debug information (equivalent to old "
		"ql2xextended_error_logging=1).\n"
		"\t\tDo LOGICAL OR of the value to enable more than one level");

int ql2xshiftctondsd = 6;
module_param(ql2xshiftctondsd, int, S_IRUGO);
MODULE_PARM_DESC(ql2xshiftctondsd,
		"Set to control shifting of command type processing "
		"based on total number of SG elements.");

int ql2xfdmienable=1;
module_param(ql2xfdmienable, int, S_IRUGO|S_IWUSR);
module_param_named(fdmi, ql2xfdmienable, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xfdmienable,
		"Enables FDMI registrations. "
		"0 - no FDMI. Default is 1 - perform FDMI.");

#define MAX_Q_DEPTH	64
static int ql2xmaxqdepth = MAX_Q_DEPTH;
module_param(ql2xmaxqdepth, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xmaxqdepth,
		"Maximum queue depth to set for each LUN. "
		"Default is 64.");

#if (IS_ENABLED(CONFIG_NVME_FC))
int ql2xenabledif;
#else
int ql2xenabledif = 2;
#endif
module_param(ql2xenabledif, int, S_IRUGO);
MODULE_PARM_DESC(ql2xenabledif,
		" Enable T10-CRC-DIF:\n"
		" Default is 2.\n"
		"  0 -- No DIF Support\n"
		"  1 -- Enable DIF for all types\n"
		"  2 -- Enable DIF for all types, except Type 0.\n");

#if (IS_ENABLED(CONFIG_NVME_FC))
int ql2xnvmeenable = 1;
#else
int ql2xnvmeenable;
#endif
module_param(ql2xnvmeenable, int, 0644);
MODULE_PARM_DESC(ql2xnvmeenable,
    "Enables NVME support. "
    "0 - no NVMe.  Default is Y");

int ql2xenablehba_err_chk = 2;
module_param(ql2xenablehba_err_chk, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xenablehba_err_chk,
		" Enable T10-CRC-DIF Error isolation by HBA:\n"
		" Default is 2.\n"
		"  0 -- Error isolation disabled\n"
		"  1 -- Error isolation enabled only for DIX Type 0\n"
		"  2 -- Error isolation enabled for all Types\n");

int ql2xiidmaenable=1;
module_param(ql2xiidmaenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xiidmaenable,
		"Enables iIDMA settings "
		"Default is 1 - perform iIDMA. 0 - no iIDMA.");

int ql2xmqsupport = 1;
module_param(ql2xmqsupport, int, S_IRUGO);
MODULE_PARM_DESC(ql2xmqsupport,
		"Enable on demand multiple queue pairs support "
		"Default is 1 for supported. "
		"Set it to 0 to turn off mq qpair support.");

int ql2xfwloadbin;
module_param(ql2xfwloadbin, int, S_IRUGO|S_IWUSR);
module_param_named(fwload, ql2xfwloadbin, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xfwloadbin,
		"Option to specify location from which to load ISP firmware:.\n"
		" 2 -- load firmware via the request_firmware() (hotplug).\n"
		"      interface.\n"
		" 1 -- load firmware from flash.\n"
		" 0 -- use default semantics.\n");

int ql2xetsenable;
module_param(ql2xetsenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xetsenable,
		"Enables firmware ETS burst."
		"Default is 0 - skip ETS enablement.");

int ql2xdbwr = 1;
module_param(ql2xdbwr, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xdbwr,
		"Option to specify scheme for request queue posting.\n"
		" 0 -- Regular doorbell.\n"
		" 1 -- CAMRAM doorbell (faster).\n");

int ql2xtargetreset = 1;
module_param(ql2xtargetreset, int, S_IRUGO);
MODULE_PARM_DESC(ql2xtargetreset,
		 "Enable target reset."
		 "Default is 1 - use hw defaults.");

int ql2xgffidenable;
module_param(ql2xgffidenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xgffidenable,
		"Enables GFF_ID checks of port type. "
		"Default is 0 - Do not use GFF_ID information.");

int ql2xasynctmfenable = 1;
module_param(ql2xasynctmfenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xasynctmfenable,
		"Enables issue of TM IOCBs asynchronously via IOCB mechanism"
		"Default is 0 - Issue TM IOCBs via mailbox mechanism.");

int ql2xdontresethba;
module_param(ql2xdontresethba, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xdontresethba,
		"Option to specify reset behaviour.\n"
		" 0 (Default) -- Reset on failure.\n"
		" 1 -- Do not reset on failure.\n");

uint64_t ql2xmaxlun = MAX_LUNS;
module_param(ql2xmaxlun, ullong, S_IRUGO);
MODULE_PARM_DESC(ql2xmaxlun,
		"Defines the maximum LU number to register with the SCSI "
		"midlayer. Default is 65535.");

int ql2xmdcapmask = 0x1F;
module_param(ql2xmdcapmask, int, S_IRUGO);
MODULE_PARM_DESC(ql2xmdcapmask,
		"Set the Minidump driver capture mask level. "
		"Default is 0x1F - Can be set to 0x3, 0x7, 0xF, 0x1F, 0x7F.");

int ql2xmdenable = 1;
module_param(ql2xmdenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xmdenable,
		"Enable/disable MiniDump. "
		"0 - MiniDump disabled. "
		"1 (Default) - MiniDump enabled.");

int ql2xexlogins = 0;
module_param(ql2xexlogins, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xexlogins,
		 "Number of extended Logins. "
		 "0 (Default)- Disabled.");

int ql2xexchoffld = 1024;
module_param(ql2xexchoffld, uint, 0644);
MODULE_PARM_DESC(ql2xexchoffld,
	"Number of target exchanges.");

int ql2xiniexchg = 1024;
module_param(ql2xiniexchg, uint, 0644);
MODULE_PARM_DESC(ql2xiniexchg,
	"Number of initiator exchanges.");

int ql2xfwholdabts = 0;
module_param(ql2xfwholdabts, int, S_IRUGO);
MODULE_PARM_DESC(ql2xfwholdabts,
		"Allow FW to hold status IOCB until ABTS rsp received. "
		"0 (Default) Do not set fw option. "
		"1 - Set fw option to hold ABTS.");

int ql2xmvasynctoatio = 1;
module_param(ql2xmvasynctoatio, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xmvasynctoatio,
		"Move PUREX, ABTS RX and RIDA IOCBs to ATIOQ"
		"0 (Default). Do not move IOCBs"
		"1 - Move IOCBs.");

int ql2xautodetectsfp = 1;
module_param(ql2xautodetectsfp, int, 0444);
MODULE_PARM_DESC(ql2xautodetectsfp,
		 "Detect SFP range and set appropriate distance.\n"
		 "1 (Default): Enable\n");

int ql2xenablemsix = 1;
module_param(ql2xenablemsix, int, 0444);
MODULE_PARM_DESC(ql2xenablemsix,
		 "Set to enable MSI or MSI-X interrupt mechanism.\n"
		 " Default is 1, enable MSI-X interrupt mechanism.\n"
		 " 0 -- enable traditional pin-based mechanism.\n"
		 " 1 -- enable MSI-X interrupt mechanism.\n"
		 " 2 -- enable MSI interrupt mechanism.\n");

int qla2xuseresexchforels;
module_param(qla2xuseresexchforels, int, 0444);
MODULE_PARM_DESC(qla2xuseresexchforels,
		 "Reserve 1/2 of emergency exchanges for ELS.\n"
		 " 0 (default): disabled");

/*
 * SCSI host template entry points
 */
static int qla2xxx_slave_configure(struct scsi_device * device);
static int qla2xxx_slave_alloc(struct scsi_device *);
static int qla2xxx_scan_finished(struct Scsi_Host *, unsigned long time);
static void qla2xxx_scan_start(struct Scsi_Host *);
static void qla2xxx_slave_destroy(struct scsi_device *);
static int qla2xxx_queuecommand(struct Scsi_Host *h, struct scsi_cmnd *cmd);
static int qla2xxx_eh_abort(struct scsi_cmnd *);
static int qla2xxx_eh_device_reset(struct scsi_cmnd *);
static int qla2xxx_eh_target_reset(struct scsi_cmnd *);
static int qla2xxx_eh_bus_reset(struct scsi_cmnd *);
static int qla2xxx_eh_host_reset(struct scsi_cmnd *);

static void qla2x00_clear_drv_active(struct qla_hw_data *);
static void qla2x00_free_device(scsi_qla_host_t *);
static int qla2xxx_map_queues(struct Scsi_Host *shost);
static void qla2x00_destroy_deferred_work(struct qla_hw_data *);


struct scsi_host_template qla2xxx_driver_template = {
	.module			= THIS_MODULE,
	.name			= QLA2XXX_DRIVER_NAME,
	.queuecommand		= qla2xxx_queuecommand,

	.eh_timed_out		= fc_eh_timed_out,
	.eh_abort_handler	= qla2xxx_eh_abort,
	.eh_device_reset_handler = qla2xxx_eh_device_reset,
	.eh_target_reset_handler = qla2xxx_eh_target_reset,
	.eh_bus_reset_handler	= qla2xxx_eh_bus_reset,
	.eh_host_reset_handler	= qla2xxx_eh_host_reset,

	.slave_configure	= qla2xxx_slave_configure,

	.slave_alloc		= qla2xxx_slave_alloc,
	.slave_destroy		= qla2xxx_slave_destroy,
	.scan_finished		= qla2xxx_scan_finished,
	.scan_start		= qla2xxx_scan_start,
	.change_queue_depth	= scsi_change_queue_depth,
	.map_queues             = qla2xxx_map_queues,
	.this_id		= -1,
	.cmd_per_lun		= 3,
	.use_clustering		= ENABLE_CLUSTERING,
	.sg_tablesize		= SG_ALL,

	.max_sectors		= 0xFFFF,
	.shost_attrs		= qla2x00_host_attrs,

	.supported_mode		= MODE_INITIATOR,
	.track_queue_depth	= 1,
};

static struct scsi_transport_template *qla2xxx_transport_template = NULL;
struct scsi_transport_template *qla2xxx_transport_vport_template = NULL;

/* TODO Convert to inlines
 *
 * Timer routines
 */

__inline__ void
qla2x00_start_timer(scsi_qla_host_t *vha, unsigned long interval)
{
	timer_setup(&vha->timer, qla2x00_timer, 0);
	vha->timer.expires = jiffies + interval * HZ;
	add_timer(&vha->timer);
	vha->timer_active = 1;
}

static inline void
qla2x00_restart_timer(scsi_qla_host_t *vha, unsigned long interval)
{
	/* Currently used for 82XX only. */
	if (vha->device_flags & DFLG_DEV_FAILED) {
		ql_dbg(ql_dbg_timer, vha, 0x600d,
		    "Device in a failed state, returning.\n");
		return;
	}

	mod_timer(&vha->timer, jiffies + interval * HZ);
}

static __inline__ void
qla2x00_stop_timer(scsi_qla_host_t *vha)
{
	del_timer_sync(&vha->timer);
	vha->timer_active = 0;
}

static int qla2x00_do_dpc(void *data);

static void qla2x00_rst_aen(scsi_qla_host_t *);

static int qla2x00_mem_alloc(struct qla_hw_data *, uint16_t, uint16_t,
	struct req_que **, struct rsp_que **);
static void qla2x00_free_fw_dump(struct qla_hw_data *);
static void qla2x00_mem_free(struct qla_hw_data *);
int qla2xxx_mqueuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd,
	struct qla_qpair *qpair);

/* -------------------------------------------------------------------------- */
static void qla_init_base_qpair(struct scsi_qla_host *vha, struct req_que *req,
    struct rsp_que *rsp)
{
	struct qla_hw_data *ha = vha->hw;
	rsp->qpair = ha->base_qpair;
	rsp->req = req;
	ha->base_qpair->req = req;
	ha->base_qpair->rsp = rsp;
	ha->base_qpair->vha = vha;
	ha->base_qpair->qp_lock_ptr = &ha->hardware_lock;
	ha->base_qpair->use_shadow_reg = IS_SHADOW_REG_CAPABLE(ha) ? 1 : 0;
	ha->base_qpair->msix = &ha->msix_entries[QLA_MSIX_RSP_Q];
	INIT_LIST_HEAD(&ha->base_qpair->hints_list);
	ha->base_qpair->enable_class_2 = ql2xenableclass2;
	/* init qpair to this cpu. Will adjust at run time. */
	qla_cpu_update(rsp->qpair, raw_smp_processor_id());
	ha->base_qpair->pdev = ha->pdev;

	if (IS_QLA27XX(ha) || IS_QLA83XX(ha))
		ha->base_qpair->reqq_start_iocbs = qla_83xx_start_iocbs;
}

static int qla2x00_alloc_queues(struct qla_hw_data *ha, struct req_que *req,
				struct rsp_que *rsp)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	ha->req_q_map = kcalloc(ha->max_req_queues, sizeof(struct req_que *),
				GFP_KERNEL);
	if (!ha->req_q_map) {
		ql_log(ql_log_fatal, vha, 0x003b,
		    "Unable to allocate memory for request queue ptrs.\n");
		goto fail_req_map;
	}

	ha->rsp_q_map = kcalloc(ha->max_rsp_queues, sizeof(struct rsp_que *),
				GFP_KERNEL);
	if (!ha->rsp_q_map) {
		ql_log(ql_log_fatal, vha, 0x003c,
		    "Unable to allocate memory for response queue ptrs.\n");
		goto fail_rsp_map;
	}

	ha->base_qpair = kzalloc(sizeof(struct qla_qpair), GFP_KERNEL);
	if (ha->base_qpair == NULL) {
		ql_log(ql_log_warn, vha, 0x00e0,
		    "Failed to allocate base queue pair memory.\n");
		goto fail_base_qpair;
	}

	qla_init_base_qpair(vha, req, rsp);

	if ((ql2xmqsupport || ql2xnvmeenable) && ha->max_qpairs) {
		ha->queue_pair_map = kcalloc(ha->max_qpairs, sizeof(struct qla_qpair *),
			GFP_KERNEL);
		if (!ha->queue_pair_map) {
			ql_log(ql_log_fatal, vha, 0x0180,
			    "Unable to allocate memory for queue pair ptrs.\n");
			goto fail_qpair_map;
		}
	}

	/*
	 * Make sure we record at least the request and response queue zero in
	 * case we need to free them if part of the probe fails.
	 */
	ha->rsp_q_map[0] = rsp;
	ha->req_q_map[0] = req;
	set_bit(0, ha->rsp_qid_map);
	set_bit(0, ha->req_qid_map);
	return 0;

fail_qpair_map:
	kfree(ha->base_qpair);
	ha->base_qpair = NULL;
fail_base_qpair:
	kfree(ha->rsp_q_map);
	ha->rsp_q_map = NULL;
fail_rsp_map:
	kfree(ha->req_q_map);
	ha->req_q_map = NULL;
fail_req_map:
	return -ENOMEM;
}

static void qla2x00_free_req_que(struct qla_hw_data *ha, struct req_que *req)
{
	if (IS_QLAFX00(ha)) {
		if (req && req->ring_fx00)
			dma_free_coherent(&ha->pdev->dev,
			    (req->length_fx00 + 1) * sizeof(request_t),
			    req->ring_fx00, req->dma_fx00);
	} else if (req && req->ring)
		dma_free_coherent(&ha->pdev->dev,
		(req->length + 1) * sizeof(request_t),
		req->ring, req->dma);

	if (req)
		kfree(req->outstanding_cmds);

	kfree(req);
}

static void qla2x00_free_rsp_que(struct qla_hw_data *ha, struct rsp_que *rsp)
{
	if (IS_QLAFX00(ha)) {
		if (rsp && rsp->ring_fx00)
			dma_free_coherent(&ha->pdev->dev,
			    (rsp->length_fx00 + 1) * sizeof(request_t),
			    rsp->ring_fx00, rsp->dma_fx00);
	} else if (rsp && rsp->ring) {
		dma_free_coherent(&ha->pdev->dev,
		(rsp->length + 1) * sizeof(response_t),
		rsp->ring, rsp->dma);
	}
	kfree(rsp);
}

static void qla2x00_free_queues(struct qla_hw_data *ha)
{
	struct req_que *req;
	struct rsp_que *rsp;
	int cnt;
	unsigned long flags;

	if (ha->queue_pair_map) {
		kfree(ha->queue_pair_map);
		ha->queue_pair_map = NULL;
	}
	if (ha->base_qpair) {
		kfree(ha->base_qpair);
		ha->base_qpair = NULL;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (cnt = 0; cnt < ha->max_req_queues; cnt++) {
		if (!test_bit(cnt, ha->req_qid_map))
			continue;

		req = ha->req_q_map[cnt];
		clear_bit(cnt, ha->req_qid_map);
		ha->req_q_map[cnt] = NULL;

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		qla2x00_free_req_que(ha, req);
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	kfree(ha->req_q_map);
	ha->req_q_map = NULL;


	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (cnt = 0; cnt < ha->max_rsp_queues; cnt++) {
		if (!test_bit(cnt, ha->rsp_qid_map))
			continue;

		rsp = ha->rsp_q_map[cnt];
		clear_bit(cnt, ha->rsp_qid_map);
		ha->rsp_q_map[cnt] =  NULL;
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		qla2x00_free_rsp_que(ha, rsp);
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	kfree(ha->rsp_q_map);
	ha->rsp_q_map = NULL;
}

static char *
qla2x00_pci_info_str(struct scsi_qla_host *vha, char *str)
{
	struct qla_hw_data *ha = vha->hw;
	static char *pci_bus_modes[] = {
		"33", "66", "100", "133",
	};
	uint16_t pci_bus;

	strcpy(str, "PCI");
	pci_bus = (ha->pci_attr & (BIT_9 | BIT_10)) >> 9;
	if (pci_bus) {
		strcat(str, "-X (");
		strcat(str, pci_bus_modes[pci_bus]);
	} else {
		pci_bus = (ha->pci_attr & BIT_8) >> 8;
		strcat(str, " (");
		strcat(str, pci_bus_modes[pci_bus]);
	}
	strcat(str, " MHz)");

	return (str);
}

static char *
qla24xx_pci_info_str(struct scsi_qla_host *vha, char *str)
{
	static char *pci_bus_modes[] = { "33", "66", "100", "133", };
	struct qla_hw_data *ha = vha->hw;
	uint32_t pci_bus;

	if (pci_is_pcie(ha->pdev)) {
		char lwstr[6];
		uint32_t lstat, lspeed, lwidth;

		pcie_capability_read_dword(ha->pdev, PCI_EXP_LNKCAP, &lstat);
		lspeed = lstat & PCI_EXP_LNKCAP_SLS;
		lwidth = (lstat & PCI_EXP_LNKCAP_MLW) >> 4;

		strcpy(str, "PCIe (");
		switch (lspeed) {
		case 1:
			strcat(str, "2.5GT/s ");
			break;
		case 2:
			strcat(str, "5.0GT/s ");
			break;
		case 3:
			strcat(str, "8.0GT/s ");
			break;
		default:
			strcat(str, "<unknown> ");
			break;
		}
		snprintf(lwstr, sizeof(lwstr), "x%d)", lwidth);
		strcat(str, lwstr);

		return str;
	}

	strcpy(str, "PCI");
	pci_bus = (ha->pci_attr & CSRX_PCIX_BUS_MODE_MASK) >> 8;
	if (pci_bus == 0 || pci_bus == 8) {
		strcat(str, " (");
		strcat(str, pci_bus_modes[pci_bus >> 3]);
	} else {
		strcat(str, "-X ");
		if (pci_bus & BIT_2)
			strcat(str, "Mode 2");
		else
			strcat(str, "Mode 1");
		strcat(str, " (");
		strcat(str, pci_bus_modes[pci_bus & ~BIT_2]);
	}
	strcat(str, " MHz)");

	return str;
}

static char *
qla2x00_fw_version_str(struct scsi_qla_host *vha, char *str, size_t size)
{
	char un_str[10];
	struct qla_hw_data *ha = vha->hw;

	snprintf(str, size, "%d.%02d.%02d ", ha->fw_major_version,
	    ha->fw_minor_version, ha->fw_subminor_version);

	if (ha->fw_attributes & BIT_9) {
		strcat(str, "FLX");
		return (str);
	}

	switch (ha->fw_attributes & 0xFF) {
	case 0x7:
		strcat(str, "EF");
		break;
	case 0x17:
		strcat(str, "TP");
		break;
	case 0x37:
		strcat(str, "IP");
		break;
	case 0x77:
		strcat(str, "VI");
		break;
	default:
		sprintf(un_str, "(%x)", ha->fw_attributes);
		strcat(str, un_str);
		break;
	}
	if (ha->fw_attributes & 0x100)
		strcat(str, "X");

	return (str);
}

static char *
qla24xx_fw_version_str(struct scsi_qla_host *vha, char *str, size_t size)
{
	struct qla_hw_data *ha = vha->hw;

	snprintf(str, size, "%d.%02d.%02d (%x)", ha->fw_major_version,
	    ha->fw_minor_version, ha->fw_subminor_version, ha->fw_attributes);
	return str;
}

void
qla2x00_sp_free_dma(void *ptr)
{
	srb_t *sp = ptr;
	struct qla_hw_data *ha = sp->vha->hw;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	void *ctx = GET_CMD_CTX_SP(sp);

	if (sp->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		sp->flags &= ~SRB_DMA_VALID;
	}

	if (sp->flags & SRB_CRC_PROT_DMA_VALID) {
		dma_unmap_sg(&ha->pdev->dev, scsi_prot_sglist(cmd),
		    scsi_prot_sg_count(cmd), cmd->sc_data_direction);
		sp->flags &= ~SRB_CRC_PROT_DMA_VALID;
	}

	if (!ctx)
		goto end;

	if (sp->flags & SRB_CRC_CTX_DSD_VALID) {
		/* List assured to be having elements */
		qla2x00_clean_dsd_pool(ha, ctx);
		sp->flags &= ~SRB_CRC_CTX_DSD_VALID;
	}

	if (sp->flags & SRB_CRC_CTX_DMA_VALID) {
		struct crc_context *ctx0 = ctx;

		dma_pool_free(ha->dl_dma_pool, ctx0, ctx0->crc_ctx_dma);
		sp->flags &= ~SRB_CRC_CTX_DMA_VALID;
	}

	if (sp->flags & SRB_FCP_CMND_DMA_VALID) {
		struct ct6_dsd *ctx1 = ctx;

		dma_pool_free(ha->fcp_cmnd_dma_pool, ctx1->fcp_cmnd,
		    ctx1->fcp_cmnd_dma);
		list_splice(&ctx1->dsd_list, &ha->gbl_dsd_list);
		ha->gbl_dsd_inuse -= ctx1->dsd_use_cnt;
		ha->gbl_dsd_avail += ctx1->dsd_use_cnt;
		mempool_free(ctx1, ha->ctx_mempool);
	}

end:
	if (sp->type != SRB_NVME_CMD && sp->type != SRB_NVME_LS) {
		CMD_SP(cmd) = NULL;
		qla2x00_rel_sp(sp);
	}
}

void
qla2x00_sp_compl(void *ptr, int res)
{
	srb_t *sp = ptr;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);

	cmd->result = res;

	if (atomic_read(&sp->ref_count) == 0) {
		ql_dbg(ql_dbg_io, sp->vha, 0x3015,
		    "SP reference-count to ZERO -- sp=%p cmd=%p.\n",
		    sp, GET_CMD_SP(sp));
		if (ql2xextended_error_logging & ql_dbg_io)
			WARN_ON(atomic_read(&sp->ref_count) == 0);
		return;
	}
	if (!atomic_dec_and_test(&sp->ref_count))
		return;

	sp->free(sp);
	cmd->scsi_done(cmd);
}

void
qla2xxx_qpair_sp_free_dma(void *ptr)
{
	srb_t *sp = (srb_t *)ptr;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	struct qla_hw_data *ha = sp->fcport->vha->hw;
	void *ctx = GET_CMD_CTX_SP(sp);

	if (sp->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		sp->flags &= ~SRB_DMA_VALID;
	}

	if (sp->flags & SRB_CRC_PROT_DMA_VALID) {
		dma_unmap_sg(&ha->pdev->dev, scsi_prot_sglist(cmd),
		    scsi_prot_sg_count(cmd), cmd->sc_data_direction);
		sp->flags &= ~SRB_CRC_PROT_DMA_VALID;
	}

	if (!ctx)
		goto end;

	if (sp->flags & SRB_CRC_CTX_DSD_VALID) {
		/* List assured to be having elements */
		qla2x00_clean_dsd_pool(ha, ctx);
		sp->flags &= ~SRB_CRC_CTX_DSD_VALID;
	}

	if (sp->flags & SRB_CRC_CTX_DMA_VALID) {
		struct crc_context *ctx0 = ctx;

		dma_pool_free(ha->dl_dma_pool, ctx, ctx0->crc_ctx_dma);
		sp->flags &= ~SRB_CRC_CTX_DMA_VALID;
	}

	if (sp->flags & SRB_FCP_CMND_DMA_VALID) {
		struct ct6_dsd *ctx1 = ctx;
		dma_pool_free(ha->fcp_cmnd_dma_pool, ctx1->fcp_cmnd,
		    ctx1->fcp_cmnd_dma);
		list_splice(&ctx1->dsd_list, &ha->gbl_dsd_list);
		ha->gbl_dsd_inuse -= ctx1->dsd_use_cnt;
		ha->gbl_dsd_avail += ctx1->dsd_use_cnt;
		mempool_free(ctx1, ha->ctx_mempool);
	}
end:
	CMD_SP(cmd) = NULL;
	qla2xxx_rel_qpair_sp(sp->qpair, sp);
}

void
qla2xxx_qpair_sp_compl(void *ptr, int res)
{
	srb_t *sp = ptr;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);

	cmd->result = res;

	if (atomic_read(&sp->ref_count) == 0) {
		ql_dbg(ql_dbg_io, sp->fcport->vha, 0x3079,
		    "SP reference-count to ZERO -- sp=%p cmd=%p.\n",
		    sp, GET_CMD_SP(sp));
		if (ql2xextended_error_logging & ql_dbg_io)
			WARN_ON(atomic_read(&sp->ref_count) == 0);
		return;
	}
	if (!atomic_dec_and_test(&sp->ref_count))
		return;

	sp->free(sp);
	cmd->scsi_done(cmd);
}

/* If we are SP1 here, we need to still take and release the host_lock as SP1
 * does not have the changes necessary to avoid taking host->host_lock.
 */
static int
qla2xxx_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *vha = shost_priv(host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmd->device));
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	srb_t *sp;
	int rval;
	struct qla_qpair *qpair = NULL;
	uint32_t tag;
	uint16_t hwq;

	if (unlikely(test_bit(UNLOADING, &base_vha->dpc_flags))) {
		cmd->result = DID_NO_CONNECT << 16;
		goto qc24_fail_command;
	}

	if (ha->mqenable) {
		if (shost_use_blk_mq(vha->host)) {
			tag = blk_mq_unique_tag(cmd->request);
			hwq = blk_mq_unique_tag_to_hwq(tag);
			qpair = ha->queue_pair_map[hwq];
		} else if (vha->vp_idx && vha->qpair) {
			qpair = vha->qpair;
		}

		if (qpair)
			return qla2xxx_mqueuecommand(host, cmd, qpair);
	}

	if (ha->flags.eeh_busy) {
		if (ha->flags.pci_channel_io_perm_failure) {
			ql_dbg(ql_dbg_aer, vha, 0x9010,
			    "PCI Channel IO permanent failure, exiting "
			    "cmd=%p.\n", cmd);
			cmd->result = DID_NO_CONNECT << 16;
		} else {
			ql_dbg(ql_dbg_aer, vha, 0x9011,
			    "EEH_Busy, Requeuing the cmd=%p.\n", cmd);
			cmd->result = DID_REQUEUE << 16;
		}
		goto qc24_fail_command;
	}

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		cmd->result = rval;
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x3003,
		    "fc_remote_port_chkready failed for cmd=%p, rval=0x%x.\n",
		    cmd, rval);
		goto qc24_fail_command;
	}

	if (!vha->flags.difdix_supported &&
		scsi_get_prot_op(cmd) != SCSI_PROT_NORMAL) {
			ql_dbg(ql_dbg_io, vha, 0x3004,
			    "DIF Cap not reg, fail DIF capable cmd's:%p.\n",
			    cmd);
			cmd->result = DID_NO_CONNECT << 16;
			goto qc24_fail_command;
	}

	if (!fcport) {
		cmd->result = DID_NO_CONNECT << 16;
		goto qc24_fail_command;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD ||
			atomic_read(&base_vha->loop_state) == LOOP_DEAD) {
			ql_dbg(ql_dbg_io, vha, 0x3005,
			    "Returning DNC, fcport_state=%d loop_state=%d.\n",
			    atomic_read(&fcport->state),
			    atomic_read(&base_vha->loop_state));
			cmd->result = DID_NO_CONNECT << 16;
			goto qc24_fail_command;
		}
		goto qc24_target_busy;
	}

	/*
	 * Return target busy if we've received a non-zero retry_delay_timer
	 * in a FCP_RSP.
	 */
	if (fcport->retry_delay_timestamp == 0) {
		/* retry delay not set */
	} else if (time_after(jiffies, fcport->retry_delay_timestamp))
		fcport->retry_delay_timestamp = 0;
	else
		goto qc24_target_busy;

	sp = qla2x00_get_sp(vha, fcport, GFP_ATOMIC);
	if (!sp)
		goto qc24_host_busy;

	sp->u.scmd.cmd = cmd;
	sp->type = SRB_SCSI_CMD;
	atomic_set(&sp->ref_count, 1);
	CMD_SP(cmd) = (void *)sp;
	sp->free = qla2x00_sp_free_dma;
	sp->done = qla2x00_sp_compl;

	rval = ha->isp_ops->start_scsi(sp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x3013,
		    "Start scsi failed rval=%d for cmd=%p.\n", rval, cmd);
		goto qc24_host_busy_free_sp;
	}

	return 0;

qc24_host_busy_free_sp:
	sp->free(sp);

qc24_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

qc24_target_busy:
	return SCSI_MLQUEUE_TARGET_BUSY;

qc24_fail_command:
	cmd->scsi_done(cmd);

	return 0;
}

/* For MQ supported I/O */
int
qla2xxx_mqueuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd,
    struct qla_qpair *qpair)
{
	scsi_qla_host_t *vha = shost_priv(host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmd->device));
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	srb_t *sp;
	int rval;

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		cmd->result = rval;
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x3076,
		    "fc_remote_port_chkready failed for cmd=%p, rval=0x%x.\n",
		    cmd, rval);
		goto qc24_fail_command;
	}

	if (!fcport) {
		cmd->result = DID_NO_CONNECT << 16;
		goto qc24_fail_command;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD ||
			atomic_read(&base_vha->loop_state) == LOOP_DEAD) {
			ql_dbg(ql_dbg_io, vha, 0x3077,
			    "Returning DNC, fcport_state=%d loop_state=%d.\n",
			    atomic_read(&fcport->state),
			    atomic_read(&base_vha->loop_state));
			cmd->result = DID_NO_CONNECT << 16;
			goto qc24_fail_command;
		}
		goto qc24_target_busy;
	}

	/*
	 * Return target busy if we've received a non-zero retry_delay_timer
	 * in a FCP_RSP.
	 */
	if (fcport->retry_delay_timestamp == 0) {
		/* retry delay not set */
	} else if (time_after(jiffies, fcport->retry_delay_timestamp))
		fcport->retry_delay_timestamp = 0;
	else
		goto qc24_target_busy;

	sp = qla2xxx_get_qpair_sp(qpair, fcport, GFP_ATOMIC);
	if (!sp)
		goto qc24_host_busy;

	sp->u.scmd.cmd = cmd;
	sp->type = SRB_SCSI_CMD;
	atomic_set(&sp->ref_count, 1);
	CMD_SP(cmd) = (void *)sp;
	sp->free = qla2xxx_qpair_sp_free_dma;
	sp->done = qla2xxx_qpair_sp_compl;
	sp->qpair = qpair;

	rval = ha->isp_ops->start_scsi_mq(sp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x3078,
		    "Start scsi failed rval=%d for cmd=%p.\n", rval, cmd);
		if (rval == QLA_INTERFACE_ERROR)
			goto qc24_free_sp_fail_command;
		goto qc24_host_busy_free_sp;
	}

	return 0;

qc24_host_busy_free_sp:
	sp->free(sp);

qc24_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

qc24_target_busy:
	return SCSI_MLQUEUE_TARGET_BUSY;

qc24_free_sp_fail_command:
	sp->free(sp);
	CMD_SP(cmd) = NULL;
	qla2xxx_rel_qpair_sp(sp->qpair, sp);

qc24_fail_command:
	cmd->scsi_done(cmd);

	return 0;
}

/*
 * qla2x00_eh_wait_on_command
 *    Waits for the command to be returned by the Firmware for some
 *    max time.
 *
 * Input:
 *    cmd = Scsi Command to wait on.
 *
 * Return:
 *    Not Found : 0
 *    Found : 1
 */
static int
qla2x00_eh_wait_on_command(struct scsi_cmnd *cmd)
{
#define ABORT_POLLING_PERIOD	1000
#define ABORT_WAIT_ITER		((2 * 1000) / (ABORT_POLLING_PERIOD))
	unsigned long wait_iter = ABORT_WAIT_ITER;
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	struct qla_hw_data *ha = vha->hw;
	int ret = QLA_SUCCESS;

	if (unlikely(pci_channel_offline(ha->pdev)) || ha->flags.eeh_busy) {
		ql_dbg(ql_dbg_taskm, vha, 0x8005,
		    "Return:eh_wait.\n");
		return ret;
	}

	while (CMD_SP(cmd) && wait_iter--) {
		msleep(ABORT_POLLING_PERIOD);
	}
	if (CMD_SP(cmd))
		ret = QLA_FUNCTION_FAILED;

	return ret;
}

/*
 * qla2x00_wait_for_hba_online
 *    Wait till the HBA is online after going through
 *    <= MAX_RETRIES_OF_ISP_ABORT  or
 *    finally HBA is disabled ie marked offline
 *
 * Input:
 *     ha - pointer to host adapter structure
 *
 * Note:
 *    Does context switching-Release SPIN_LOCK
 *    (if any) before calling this routine.
 *
 * Return:
 *    Success (Adapter is online) : 0
 *    Failed  (Adapter is offline/disabled) : 1
 */
int
qla2x00_wait_for_hba_online(scsi_qla_host_t *vha)
{
	int		return_status;
	unsigned long	wait_online;
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	wait_online = jiffies + (MAX_LOOP_TIMEOUT * HZ);
	while (((test_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags)) ||
	    test_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &base_vha->dpc_flags) ||
	    ha->dpc_active) && time_before(jiffies, wait_online)) {

		msleep(1000);
	}
	if (base_vha->flags.online)
		return_status = QLA_SUCCESS;
	else
		return_status = QLA_FUNCTION_FAILED;

	return (return_status);
}

static inline int test_fcport_count(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;
	int res;

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	ql_dbg(ql_dbg_init, vha, 0x00ec,
	    "tgt %p, fcport_count=%d\n",
	    vha, vha->fcport_count);
	res = (vha->fcport_count == 0);
	spin_unlock_irqrestore(&ha->tgt.sess_lock, flags);

	return res;
}

/*
 * qla2x00_wait_for_sess_deletion can only be called from remove_one.
 * it has dependency on UNLOADING flag to stop device discovery
 */
void
qla2x00_wait_for_sess_deletion(scsi_qla_host_t *vha)
{
	qla2x00_mark_all_devices_lost(vha, 0);

	wait_event_timeout(vha->fcport_waitQ, test_fcport_count(vha), 10*HZ);
}

/*
 * qla2x00_wait_for_hba_ready
 * Wait till the HBA is ready before doing driver unload
 *
 * Input:
 *     ha - pointer to host adapter structure
 *
 * Note:
 *    Does context switching-Release SPIN_LOCK
 *    (if any) before calling this routine.
 *
 */
static void
qla2x00_wait_for_hba_ready(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	while ((qla2x00_reset_active(vha) || ha->dpc_active ||
		ha->flags.mbox_busy) ||
	       test_bit(FX00_RESET_RECOVERY, &vha->dpc_flags) ||
	       test_bit(FX00_TARGET_SCAN, &vha->dpc_flags)) {
		if (test_bit(UNLOADING, &base_vha->dpc_flags))
			break;
		msleep(1000);
	}
}

int
qla2x00_wait_for_chip_reset(scsi_qla_host_t *vha)
{
	int		return_status;
	unsigned long	wait_reset;
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	wait_reset = jiffies + (MAX_LOOP_TIMEOUT * HZ);
	while (((test_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags)) ||
	    test_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &base_vha->dpc_flags) ||
	    ha->dpc_active) && time_before(jiffies, wait_reset)) {

		msleep(1000);

		if (!test_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags) &&
		    ha->flags.chip_reset_done)
			break;
	}
	if (ha->flags.chip_reset_done)
		return_status = QLA_SUCCESS;
	else
		return_status = QLA_FUNCTION_FAILED;

	return return_status;
}

static void
sp_get(struct srb *sp)
{
	atomic_inc(&sp->ref_count);
}

#define ISP_REG_DISCONNECT 0xffffffffU
/**************************************************************************
* qla2x00_isp_reg_stat
*
* Description:
*	Read the host status register of ISP before aborting the command.
*
* Input:
*	ha = pointer to host adapter structure.
*
*
* Returns:
*	Either true or false.
*
* Note:	Return true if there is register disconnect.
**************************************************************************/
static inline
uint32_t qla2x00_isp_reg_stat(struct qla_hw_data *ha)
{
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	struct device_reg_82xx __iomem *reg82 = &ha->iobase->isp82;

	if (IS_P3P_TYPE(ha))
		return ((RD_REG_DWORD(&reg82->host_int)) == ISP_REG_DISCONNECT);
	else
		return ((RD_REG_DWORD(&reg->host_status)) ==
			ISP_REG_DISCONNECT);
}

/**************************************************************************
* qla2xxx_eh_abort
*
* Description:
*    The abort function will abort the specified command.
*
* Input:
*    cmd = Linux SCSI command packet to be aborted.
*
* Returns:
*    Either SUCCESS or FAILED.
*
* Note:
*    Only return FAILED if command not returned by firmware.
**************************************************************************/
static int
qla2xxx_eh_abort(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	srb_t *sp;
	int ret;
	unsigned int id;
	uint64_t lun;
	unsigned long flags;
	int rval, wait = 0;
	struct qla_hw_data *ha = vha->hw;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x8042,
		    "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}
	if (!CMD_SP(cmd))
		return SUCCESS;

	ret = fc_block_scsi_eh(cmd);
	if (ret != 0)
		return ret;
	ret = SUCCESS;

	id = cmd->device->id;
	lun = cmd->device->lun;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	sp = (srb_t *) CMD_SP(cmd);
	if (!sp) {
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		return SUCCESS;
	}

	ql_dbg(ql_dbg_taskm, vha, 0x8002,
	    "Aborting from RISC nexus=%ld:%d:%llu sp=%p cmd=%p handle=%x\n",
	    vha->host_no, id, lun, sp, cmd, sp->handle);

	/* Get a reference to the sp and drop the lock.*/
	sp_get(sp);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	rval = ha->isp_ops->abort_command(sp);
	if (rval) {
		if (rval == QLA_FUNCTION_PARAMETER_ERROR)
			ret = SUCCESS;
		else
			ret = FAILED;

		ql_dbg(ql_dbg_taskm, vha, 0x8003,
		    "Abort command mbx failed cmd=%p, rval=%x.\n", cmd, rval);
	} else {
		ql_dbg(ql_dbg_taskm, vha, 0x8004,
		    "Abort command mbx success cmd=%p.\n", cmd);
		wait = 1;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	sp->done(sp, 0);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Did the command return during mailbox execution? */
	if (ret == FAILED && !CMD_SP(cmd))
		ret = SUCCESS;

	/* Wait for the command to be returned. */
	if (wait) {
		if (qla2x00_eh_wait_on_command(cmd) != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x8006,
			    "Abort handler timed out cmd=%p.\n", cmd);
			ret = FAILED;
		}
	}

	ql_log(ql_log_info, vha, 0x801c,
	    "Abort command issued nexus=%ld:%d:%llu --  %d %x.\n",
	    vha->host_no, id, lun, wait, ret);

	return ret;
}

int
qla2x00_eh_wait_for_pending_commands(scsi_qla_host_t *vha, unsigned int t,
	uint64_t l, enum nexus_wait_type type)
{
	int cnt, match, status;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;
	srb_t *sp;
	struct scsi_cmnd *cmd;

	status = QLA_SUCCESS;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	req = vha->req;
	for (cnt = 1; status == QLA_SUCCESS &&
		cnt < req->num_outstanding_cmds; cnt++) {
		sp = req->outstanding_cmds[cnt];
		if (!sp)
			continue;
		if (sp->type != SRB_SCSI_CMD)
			continue;
		if (vha->vp_idx != sp->vha->vp_idx)
			continue;
		match = 0;
		cmd = GET_CMD_SP(sp);
		switch (type) {
		case WAIT_HOST:
			match = 1;
			break;
		case WAIT_TARGET:
			match = cmd->device->id == t;
			break;
		case WAIT_LUN:
			match = (cmd->device->id == t &&
				cmd->device->lun == l);
			break;
		}
		if (!match)
			continue;

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		status = qla2x00_eh_wait_on_command(cmd);
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return status;
}

static char *reset_errors[] = {
	"HBA not online",
	"HBA not ready",
	"Task management failed",
	"Waiting for command completions",
};

static int
__qla2xxx_eh_generic_reset(char *name, enum nexus_wait_type type,
    struct scsi_cmnd *cmd, int (*do_reset)(struct fc_port *, uint64_t, int))
{
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	int err;

	if (!fcport) {
		return FAILED;
	}

	err = fc_block_scsi_eh(cmd);
	if (err != 0)
		return err;

	ql_log(ql_log_info, vha, 0x8009,
	    "%s RESET ISSUED nexus=%ld:%d:%llu cmd=%p.\n", name, vha->host_no,
	    cmd->device->id, cmd->device->lun, cmd);

	err = 0;
	if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800a,
		    "Wait for hba online failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}
	err = 2;
	if (do_reset(fcport, cmd->device->lun, cmd->request->cpu + 1)
		!= QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800c,
		    "do_reset failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}
	err = 3;
	if (qla2x00_eh_wait_for_pending_commands(vha, cmd->device->id,
	    cmd->device->lun, type) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800d,
		    "wait for pending cmds failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}

	ql_log(ql_log_info, vha, 0x800e,
	    "%s RESET SUCCEEDED nexus:%ld:%d:%llu cmd=%p.\n", name,
	    vha->host_no, cmd->device->id, cmd->device->lun, cmd);

	return SUCCESS;

eh_reset_failed:
	ql_log(ql_log_info, vha, 0x800f,
	    "%s RESET FAILED: %s nexus=%ld:%d:%llu cmd=%p.\n", name,
	    reset_errors[err], vha->host_no, cmd->device->id, cmd->device->lun,
	    cmd);
	return FAILED;
}

static int
qla2xxx_eh_device_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	struct qla_hw_data *ha = vha->hw;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x803e,
		    "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}

	return __qla2xxx_eh_generic_reset("DEVICE", WAIT_LUN, cmd,
	    ha->isp_ops->lun_reset);
}

static int
qla2xxx_eh_target_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	struct qla_hw_data *ha = vha->hw;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x803f,
		    "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}

	return __qla2xxx_eh_generic_reset("TARGET", WAIT_TARGET, cmd,
	    ha->isp_ops->target_reset);
}

/**************************************************************************
* qla2xxx_eh_bus_reset
*
* Description:
*    The bus reset function will reset the bus and abort any executing
*    commands.
*
* Input:
*    cmd = Linux SCSI command packet of the command that cause the
*          bus reset.
*
* Returns:
*    SUCCESS/FAILURE (defined as macro in scsi.h).
*
**************************************************************************/
static int
qla2xxx_eh_bus_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	int ret = FAILED;
	unsigned int id;
	uint64_t lun;
	struct qla_hw_data *ha = vha->hw;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x8040,
		    "PCI/Register disconnect, exiting.\n");
		return FAILED;
	}

	id = cmd->device->id;
	lun = cmd->device->lun;

	if (!fcport) {
		return ret;
	}

	ret = fc_block_scsi_eh(cmd);
	if (ret != 0)
		return ret;
	ret = FAILED;

	ql_log(ql_log_info, vha, 0x8012,
	    "BUS RESET ISSUED nexus=%ld:%d:%llu.\n", vha->host_no, id, lun);

	if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0x8013,
		    "Wait for hba online failed board disabled.\n");
		goto eh_bus_reset_done;
	}

	if (qla2x00_loop_reset(vha) == QLA_SUCCESS)
		ret = SUCCESS;

	if (ret == FAILED)
		goto eh_bus_reset_done;

	/* Flush outstanding commands. */
	if (qla2x00_eh_wait_for_pending_commands(vha, 0, 0, WAIT_HOST) !=
	    QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x8014,
		    "Wait for pending commands failed.\n");
		ret = FAILED;
	}

eh_bus_reset_done:
	ql_log(ql_log_warn, vha, 0x802b,
	    "BUS RESET %s nexus=%ld:%d:%llu.\n",
	    (ret == FAILED) ? "FAILED" : "SUCCEEDED", vha->host_no, id, lun);

	return ret;
}

/**************************************************************************
* qla2xxx_eh_host_reset
*
* Description:
*    The reset function will reset the Adapter.
*
* Input:
*      cmd = Linux SCSI command packet of the command that cause the
*            adapter reset.
*
* Returns:
*      Either SUCCESS or FAILED.
*
* Note:
**************************************************************************/
static int
qla2xxx_eh_host_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	struct qla_hw_data *ha = vha->hw;
	int ret = FAILED;
	unsigned int id;
	uint64_t lun;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x8041,
		    "PCI/Register disconnect, exiting.\n");
		schedule_work(&ha->board_disable);
		return SUCCESS;
	}

	id = cmd->device->id;
	lun = cmd->device->lun;

	ql_log(ql_log_info, vha, 0x8018,
	    "ADAPTER RESET ISSUED nexus=%ld:%d:%llu.\n", vha->host_no, id, lun);

	/*
	 * No point in issuing another reset if one is active.  Also do not
	 * attempt a reset if we are updating flash.
	 */
	if (qla2x00_reset_active(vha) || ha->optrom_state != QLA_SWAITING)
		goto eh_host_reset_lock;

	if (vha != base_vha) {
		if (qla2x00_vp_abort_isp(vha))
			goto eh_host_reset_lock;
	} else {
		if (IS_P3P_TYPE(vha->hw)) {
			if (!qla82xx_fcoe_ctx_reset(vha)) {
				/* Ctx reset success */
				ret = SUCCESS;
				goto eh_host_reset_lock;
			}
			/* fall thru if ctx reset failed */
		}
		if (ha->wq)
			flush_workqueue(ha->wq);

		set_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
		if (ha->isp_ops->abort_isp(base_vha)) {
			clear_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
			/* failed. schedule dpc to try */
			set_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags);

			if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
				ql_log(ql_log_warn, vha, 0x802a,
				    "wait for hba online failed.\n");
				goto eh_host_reset_lock;
			}
		}
		clear_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
	}

	/* Waiting for command to be returned to OS.*/
	if (qla2x00_eh_wait_for_pending_commands(vha, 0, 0, WAIT_HOST) ==
		QLA_SUCCESS)
		ret = SUCCESS;

eh_host_reset_lock:
	ql_log(ql_log_info, vha, 0x8017,
	    "ADAPTER RESET %s nexus=%ld:%d:%llu.\n",
	    (ret == FAILED) ? "FAILED" : "SUCCEEDED", vha->host_no, id, lun);

	return ret;
}

/*
* qla2x00_loop_reset
*      Issue loop reset.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
int
qla2x00_loop_reset(scsi_qla_host_t *vha)
{
	int ret;
	struct fc_port *fcport;
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLAFX00(ha)) {
		return qlafx00_loop_reset(vha);
	}

	if (ql2xtargetreset == 1 && ha->flags.enable_target_reset) {
		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if (fcport->port_type != FCT_TARGET)
				continue;

			ret = ha->isp_ops->target_reset(fcport, 0, 0);
			if (ret != QLA_SUCCESS) {
				ql_dbg(ql_dbg_taskm, vha, 0x802c,
				    "Bus Reset failed: Reset=%d "
				    "d_id=%x.\n", ret, fcport->d_id.b24);
			}
		}
	}


	if (ha->flags.enable_lip_full_login && !IS_CNA_CAPABLE(ha)) {
		atomic_set(&vha->loop_state, LOOP_DOWN);
		atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
		qla2x00_mark_all_devices_lost(vha, 0);
		ret = qla2x00_full_login_lip(vha);
		if (ret != QLA_SUCCESS) {
			ql_dbg(ql_dbg_taskm, vha, 0x802d,
			    "full_login_lip=%d.\n", ret);
		}
	}

	if (ha->flags.enable_lip_reset) {
		ret = qla2x00_lip_reset(vha);
		if (ret != QLA_SUCCESS)
			ql_dbg(ql_dbg_taskm, vha, 0x802e,
			    "lip_reset failed (%d).\n", ret);
	}

	/* Issue marker command only when we are going to start the I/O */
	vha->marker_needed = 1;

	return QLA_SUCCESS;
}

static void
__qla2x00_abort_all_cmds(struct qla_qpair *qp, int res)
{
	int cnt, status;
	unsigned long flags;
	srb_t *sp;
	scsi_qla_host_t *vha = qp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_cmd *cmd;
	uint8_t trace = 0;

	if (!ha->req_q_map)
		return;
	spin_lock_irqsave(qp->qp_lock_ptr, flags);
	req = qp->req;
	for (cnt = 1; cnt < req->num_outstanding_cmds; cnt++) {
		sp = req->outstanding_cmds[cnt];
		if (sp) {
			req->outstanding_cmds[cnt] = NULL;
			if (sp->cmd_type == TYPE_SRB) {
				if (sp->type == SRB_NVME_CMD ||
				    sp->type == SRB_NVME_LS) {
					sp_get(sp);
					spin_unlock_irqrestore(qp->qp_lock_ptr,
					    flags);
					qla_nvme_abort(ha, sp, res);
					spin_lock_irqsave(qp->qp_lock_ptr,
					    flags);
				} else if (GET_CMD_SP(sp) &&
				    !ha->flags.eeh_busy &&
				    (!test_bit(ABORT_ISP_ACTIVE,
					&vha->dpc_flags)) &&
				    !qla2x00_isp_reg_stat(ha) &&
				    (sp->type == SRB_SCSI_CMD)) {
					/*
					 * Don't abort commands in
					 * adapter during EEH
					 * recovery as it's not
					 * accessible/responding.
					 *
					 * Get a reference to the sp
					 * and drop the lock. The
					 * reference ensures this
					 * sp->done() call and not the
					 * call in qla2xxx_eh_abort()
					 * ends the SCSI command (with
					 * result 'res').
					 */
					sp_get(sp);
					spin_unlock_irqrestore(qp->qp_lock_ptr,
					    flags);
					status = qla2xxx_eh_abort(
					    GET_CMD_SP(sp));
					spin_lock_irqsave(qp->qp_lock_ptr,
					    flags);
					/*
					 * Get rid of extra reference
					 * if immediate exit from
					 * ql2xxx_eh_abort
					 */
					if (status == FAILED &&
					    (qla2x00_isp_reg_stat(ha)))
						atomic_dec(
						    &sp->ref_count);
				}
				sp->done(sp, res);
			} else {
				if (!vha->hw->tgt.tgt_ops || !tgt ||
				    qla_ini_mode_enabled(vha)) {
					if (!trace)
						ql_dbg(ql_dbg_tgt_mgt,
						    vha, 0xf003,
						    "HOST-ABORT-HNDLR: dpc_flags=%lx. Target mode disabled\n",
						    vha->dpc_flags);
					continue;
				}
				cmd = (struct qla_tgt_cmd *)sp;
				qlt_abort_cmd_on_host_reset(cmd->vha, cmd);
			}
		}
	}
	spin_unlock_irqrestore(qp->qp_lock_ptr, flags);
}

void
qla2x00_abort_all_cmds(scsi_qla_host_t *vha, int res)
{
	int que;
	struct qla_hw_data *ha = vha->hw;

	__qla2x00_abort_all_cmds(ha->base_qpair, res);

	for (que = 0; que < ha->max_qpairs; que++) {
		if (!ha->queue_pair_map[que])
			continue;

		__qla2x00_abort_all_cmds(ha->queue_pair_map[que], res);
	}
}

static int
qla2xxx_slave_alloc(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	sdev->hostdata = *(fc_port_t **)rport->dd_data;

	return 0;
}

static int
qla2xxx_slave_configure(struct scsi_device *sdev)
{
	scsi_qla_host_t *vha = shost_priv(sdev->host);
	struct req_que *req = vha->req;

	if (IS_T10_PI_CAPABLE(vha->hw))
		blk_queue_update_dma_alignment(sdev->request_queue, 0x7);

	scsi_change_queue_depth(sdev, req->max_q_depth);
	return 0;
}

static void
qla2xxx_slave_destroy(struct scsi_device *sdev)
{
	sdev->hostdata = NULL;
}

/**
 * qla2x00_config_dma_addressing() - Configure OS DMA addressing method.
 * @ha: HA context
 *
 * At exit, the @ha's flags.enable_64bit_addressing set to indicated
 * supported addressing method.
 */
static void
qla2x00_config_dma_addressing(struct qla_hw_data *ha)
{
	/* Assume a 32bit DMA mask. */
	ha->flags.enable_64bit_addressing = 0;

	if (!dma_set_mask(&ha->pdev->dev, DMA_BIT_MASK(64))) {
		/* Any upper-dword bits set? */
		if (MSD(dma_get_required_mask(&ha->pdev->dev)) &&
		    !pci_set_consistent_dma_mask(ha->pdev, DMA_BIT_MASK(64))) {
			/* Ok, a 64bit DMA mask is applicable. */
			ha->flags.enable_64bit_addressing = 1;
			ha->isp_ops->calc_req_entries = qla2x00_calc_iocbs_64;
			ha->isp_ops->build_iocbs = qla2x00_build_scsi_iocbs_64;
			return;
		}
	}

	dma_set_mask(&ha->pdev->dev, DMA_BIT_MASK(32));
	pci_set_consistent_dma_mask(ha->pdev, DMA_BIT_MASK(32));
}

static void
qla2x00_enable_intrs(struct qla_hw_data *ha)
{
	unsigned long flags = 0;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 1;
	/* enable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, ICR_EN_INT | ICR_EN_RISC);
	RD_REG_WORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

}

static void
qla2x00_disable_intrs(struct qla_hw_data *ha)
{
	unsigned long flags = 0;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 0;
	/* disable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, 0);
	RD_REG_WORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void
qla24xx_enable_intrs(struct qla_hw_data *ha)
{
	unsigned long flags = 0;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 1;
	WRT_REG_DWORD(&reg->ictrl, ICRX_EN_RISC_INT);
	RD_REG_DWORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void
qla24xx_disable_intrs(struct qla_hw_data *ha)
{
	unsigned long flags = 0;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (IS_NOPOLLING_TYPE(ha))
		return;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 0;
	WRT_REG_DWORD(&reg->ictrl, 0);
	RD_REG_DWORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static int
qla2x00_iospace_config(struct qla_hw_data *ha)
{
	resource_size_t pio;
	uint16_t msix;

	if (pci_request_selected_regions(ha->pdev, ha->bars,
	    QLA2XXX_DRIVER_NAME)) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0011,
		    "Failed to reserve PIO/MMIO regions (%s), aborting.\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}
	if (!(ha->bars & 1))
		goto skip_pio;

	/* We only need PIO for Flash operations on ISP2312 v2 chips. */
	pio = pci_resource_start(ha->pdev, 0);
	if (pci_resource_flags(ha->pdev, 0) & IORESOURCE_IO) {
		if (pci_resource_len(ha->pdev, 0) < MIN_IOBASE_LEN) {
			ql_log_pci(ql_log_warn, ha->pdev, 0x0012,
			    "Invalid pci I/O region size (%s).\n",
			    pci_name(ha->pdev));
			pio = 0;
		}
	} else {
		ql_log_pci(ql_log_warn, ha->pdev, 0x0013,
		    "Region #0 no a PIO resource (%s).\n",
		    pci_name(ha->pdev));
		pio = 0;
	}
	ha->pio_address = pio;
	ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0014,
	    "PIO address=%llu.\n",
	    (unsigned long long)ha->pio_address);

skip_pio:
	/* Use MMIO operations for all accesses. */
	if (!(pci_resource_flags(ha->pdev, 1) & IORESOURCE_MEM)) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0015,
		    "Region #1 not an MMIO resource (%s), aborting.\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}
	if (pci_resource_len(ha->pdev, 1) < MIN_IOBASE_LEN) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0016,
		    "Invalid PCI mem region size (%s), aborting.\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	ha->iobase = ioremap(pci_resource_start(ha->pdev, 1), MIN_IOBASE_LEN);
	if (!ha->iobase) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0017,
		    "Cannot remap MMIO (%s), aborting.\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	/* Determine queue resources */
	ha->max_req_queues = ha->max_rsp_queues = 1;
	ha->msix_count = QLA_BASE_VECTORS;
	if (!ql2xmqsupport || !ql2xnvmeenable ||
	    (!IS_QLA25XX(ha) && !IS_QLA81XX(ha)))
		goto mqiobase_exit;

	ha->mqiobase = ioremap(pci_resource_start(ha->pdev, 3),
			pci_resource_len(ha->pdev, 3));
	if (ha->mqiobase) {
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0018,
		    "MQIO Base=%p.\n", ha->mqiobase);
		/* Read MSIX vector size of the board */
		pci_read_config_word(ha->pdev, QLA_PCI_MSIX_CONTROL, &msix);
		ha->msix_count = msix + 1;
		/* Max queues are bounded by available msix vectors */
		/* MB interrupt uses 1 vector */
		ha->max_req_queues = ha->msix_count - 1;
		ha->max_rsp_queues = ha->max_req_queues;
		/* Queue pairs is the max value minus the base queue pair */
		ha->max_qpairs = ha->max_rsp_queues - 1;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0188,
		    "Max no of queues pairs: %d.\n", ha->max_qpairs);

		ql_log_pci(ql_log_info, ha->pdev, 0x001a,
		    "MSI-X vector count: %d.\n", ha->msix_count);
	} else
		ql_log_pci(ql_log_info, ha->pdev, 0x001b,
		    "BAR 3 not enabled.\n");

mqiobase_exit:
	ql_dbg_pci(ql_dbg_init, ha->pdev, 0x001c,
	    "MSIX Count: %d.\n", ha->msix_count);
	return (0);

iospace_error_exit:
	return (-ENOMEM);
}


static int
qla83xx_iospace_config(struct qla_hw_data *ha)
{
	uint16_t msix;

	if (pci_request_selected_regions(ha->pdev, ha->bars,
	    QLA2XXX_DRIVER_NAME)) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0117,
		    "Failed to reserve PIO/MMIO regions (%s), aborting.\n",
		    pci_name(ha->pdev));

		goto iospace_error_exit;
	}

	/* Use MMIO operations for all accesses. */
	if (!(pci_resource_flags(ha->pdev, 0) & IORESOURCE_MEM)) {
		ql_log_pci(ql_log_warn, ha->pdev, 0x0118,
		    "Invalid pci I/O region size (%s).\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}
	if (pci_resource_len(ha->pdev, 0) < MIN_IOBASE_LEN) {
		ql_log_pci(ql_log_warn, ha->pdev, 0x0119,
		    "Invalid PCI mem region size (%s), aborting\n",
			pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	ha->iobase = ioremap(pci_resource_start(ha->pdev, 0), MIN_IOBASE_LEN);
	if (!ha->iobase) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x011a,
		    "Cannot remap MMIO (%s), aborting.\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	/* 64bit PCI BAR - BAR2 will correspoond to region 4 */
	/* 83XX 26XX always use MQ type access for queues
	 * - mbar 2, a.k.a region 4 */
	ha->max_req_queues = ha->max_rsp_queues = 1;
	ha->msix_count = QLA_BASE_VECTORS;
	ha->mqiobase = ioremap(pci_resource_start(ha->pdev, 4),
			pci_resource_len(ha->pdev, 4));

	if (!ha->mqiobase) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x011d,
		    "BAR2/region4 not enabled\n");
		goto mqiobase_exit;
	}

	ha->msixbase = ioremap(pci_resource_start(ha->pdev, 2),
			pci_resource_len(ha->pdev, 2));
	if (ha->msixbase) {
		/* Read MSIX vector size of the board */
		pci_read_config_word(ha->pdev,
		    QLA_83XX_PCI_MSIX_CONTROL, &msix);
		ha->msix_count = (msix & PCI_MSIX_FLAGS_QSIZE)  + 1;
		/*
		 * By default, driver uses at least two msix vectors
		 * (default & rspq)
		 */
		if (ql2xmqsupport || ql2xnvmeenable) {
			/* MB interrupt uses 1 vector */
			ha->max_req_queues = ha->msix_count - 1;

			/* ATIOQ needs 1 vector. That's 1 less QPair */
			if (QLA_TGT_MODE_ENABLED())
				ha->max_req_queues--;

			ha->max_rsp_queues = ha->max_req_queues;

			/* Queue pairs is the max value minus
			 * the base queue pair */
			ha->max_qpairs = ha->max_req_queues - 1;
			ql_dbg_pci(ql_dbg_init, ha->pdev, 0x00e3,
			    "Max no of queues pairs: %d.\n", ha->max_qpairs);
		}
		ql_log_pci(ql_log_info, ha->pdev, 0x011c,
		    "MSI-X vector count: %d.\n", ha->msix_count);
	} else
		ql_log_pci(ql_log_info, ha->pdev, 0x011e,
		    "BAR 1 not enabled.\n");

mqiobase_exit:
	ql_dbg_pci(ql_dbg_init, ha->pdev, 0x011f,
	    "MSIX Count: %d.\n", ha->msix_count);
	return 0;

iospace_error_exit:
	return -ENOMEM;
}

static struct isp_operations qla2100_isp_ops = {
	.pci_config		= qla2100_pci_config,
	.reset_chip		= qla2x00_reset_chip,
	.chip_diag		= qla2x00_chip_diag,
	.config_rings		= qla2x00_config_rings,
	.reset_adapter		= qla2x00_reset_adapter,
	.nvram_config		= qla2x00_nvram_config,
	.update_fw_options	= qla2x00_update_fw_options,
	.load_risc		= qla2x00_load_risc,
	.pci_info_str		= qla2x00_pci_info_str,
	.fw_version_str		= qla2x00_fw_version_str,
	.intr_handler		= qla2100_intr_handler,
	.enable_intrs		= qla2x00_enable_intrs,
	.disable_intrs		= qla2x00_disable_intrs,
	.abort_command		= qla2x00_abort_command,
	.target_reset		= qla2x00_abort_target,
	.lun_reset		= qla2x00_lun_reset,
	.fabric_login		= qla2x00_login_fabric,
	.fabric_logout		= qla2x00_fabric_logout,
	.calc_req_entries	= qla2x00_calc_iocbs_32,
	.build_iocbs		= qla2x00_build_scsi_iocbs_32,
	.prep_ms_iocb		= qla2x00_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla2x00_prep_ms_fdmi_iocb,
	.read_nvram		= qla2x00_read_nvram_data,
	.write_nvram		= qla2x00_write_nvram_data,
	.fw_dump		= qla2100_fw_dump,
	.beacon_on		= NULL,
	.beacon_off		= NULL,
	.beacon_blink		= NULL,
	.read_optrom		= qla2x00_read_optrom_data,
	.write_optrom		= qla2x00_write_optrom_data,
	.get_flash_version	= qla2x00_get_flash_version,
	.start_scsi		= qla2x00_start_scsi,
	.start_scsi_mq          = NULL,
	.abort_isp		= qla2x00_abort_isp,
	.iospace_config     	= qla2x00_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qla2300_isp_ops = {
	.pci_config		= qla2300_pci_config,
	.reset_chip		= qla2x00_reset_chip,
	.chip_diag		= qla2x00_chip_diag,
	.config_rings		= qla2x00_config_rings,
	.reset_adapter		= qla2x00_reset_adapter,
	.nvram_config		= qla2x00_nvram_config,
	.update_fw_options	= qla2x00_update_fw_options,
	.load_risc		= qla2x00_load_risc,
	.pci_info_str		= qla2x00_pci_info_str,
	.fw_version_str		= qla2x00_fw_version_str,
	.intr_handler		= qla2300_intr_handler,
	.enable_intrs		= qla2x00_enable_intrs,
	.disable_intrs		= qla2x00_disable_intrs,
	.abort_command		= qla2x00_abort_command,
	.target_reset		= qla2x00_abort_target,
	.lun_reset		= qla2x00_lun_reset,
	.fabric_login		= qla2x00_login_fabric,
	.fabric_logout		= qla2x00_fabric_logout,
	.calc_req_entries	= qla2x00_calc_iocbs_32,
	.build_iocbs		= qla2x00_build_scsi_iocbs_32,
	.prep_ms_iocb		= qla2x00_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla2x00_prep_ms_fdmi_iocb,
	.read_nvram		= qla2x00_read_nvram_data,
	.write_nvram		= qla2x00_write_nvram_data,
	.fw_dump		= qla2300_fw_dump,
	.beacon_on		= qla2x00_beacon_on,
	.beacon_off		= qla2x00_beacon_off,
	.beacon_blink		= qla2x00_beacon_blink,
	.read_optrom		= qla2x00_read_optrom_data,
	.write_optrom		= qla2x00_write_optrom_data,
	.get_flash_version	= qla2x00_get_flash_version,
	.start_scsi		= qla2x00_start_scsi,
	.start_scsi_mq          = NULL,
	.abort_isp		= qla2x00_abort_isp,
	.iospace_config		= qla2x00_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qla24xx_isp_ops = {
	.pci_config		= qla24xx_pci_config,
	.reset_chip		= qla24xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla24xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla24xx_nvram_config,
	.update_fw_options	= qla24xx_update_fw_options,
	.load_risc		= qla24xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla24xx_intr_handler,
	.enable_intrs		= qla24xx_enable_intrs,
	.disable_intrs		= qla24xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= qla24xx_read_nvram_data,
	.write_nvram		= qla24xx_write_nvram_data,
	.fw_dump		= qla24xx_fw_dump,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= qla24xx_beacon_blink,
	.read_optrom		= qla24xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
	.start_scsi		= qla24xx_start_scsi,
	.start_scsi_mq          = NULL,
	.abort_isp		= qla2x00_abort_isp,
	.iospace_config		= qla2x00_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qla25xx_isp_ops = {
	.pci_config		= qla25xx_pci_config,
	.reset_chip		= qla24xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla24xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla24xx_nvram_config,
	.update_fw_options	= qla24xx_update_fw_options,
	.load_risc		= qla24xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla24xx_intr_handler,
	.enable_intrs		= qla24xx_enable_intrs,
	.disable_intrs		= qla24xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= qla25xx_read_nvram_data,
	.write_nvram		= qla25xx_write_nvram_data,
	.fw_dump		= qla25xx_fw_dump,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= qla24xx_beacon_blink,
	.read_optrom		= qla25xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
	.start_scsi		= qla24xx_dif_start_scsi,
	.start_scsi_mq          = qla2xxx_dif_start_scsi_mq,
	.abort_isp		= qla2x00_abort_isp,
	.iospace_config		= qla2x00_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qla81xx_isp_ops = {
	.pci_config		= qla25xx_pci_config,
	.reset_chip		= qla24xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla24xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla81xx_nvram_config,
	.update_fw_options	= qla81xx_update_fw_options,
	.load_risc		= qla81xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla24xx_intr_handler,
	.enable_intrs		= qla24xx_enable_intrs,
	.disable_intrs		= qla24xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= NULL,
	.write_nvram		= NULL,
	.fw_dump		= qla81xx_fw_dump,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= qla83xx_beacon_blink,
	.read_optrom		= qla25xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
	.start_scsi		= qla24xx_dif_start_scsi,
	.start_scsi_mq          = qla2xxx_dif_start_scsi_mq,
	.abort_isp		= qla2x00_abort_isp,
	.iospace_config		= qla2x00_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qla82xx_isp_ops = {
	.pci_config		= qla82xx_pci_config,
	.reset_chip		= qla82xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla82xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla81xx_nvram_config,
	.update_fw_options	= qla24xx_update_fw_options,
	.load_risc		= qla82xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla82xx_intr_handler,
	.enable_intrs		= qla82xx_enable_intrs,
	.disable_intrs		= qla82xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= qla24xx_read_nvram_data,
	.write_nvram		= qla24xx_write_nvram_data,
	.fw_dump		= qla82xx_fw_dump,
	.beacon_on		= qla82xx_beacon_on,
	.beacon_off		= qla82xx_beacon_off,
	.beacon_blink		= NULL,
	.read_optrom		= qla82xx_read_optrom_data,
	.write_optrom		= qla82xx_write_optrom_data,
	.get_flash_version	= qla82xx_get_flash_version,
	.start_scsi             = qla82xx_start_scsi,
	.start_scsi_mq          = NULL,
	.abort_isp		= qla82xx_abort_isp,
	.iospace_config     	= qla82xx_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qla8044_isp_ops = {
	.pci_config		= qla82xx_pci_config,
	.reset_chip		= qla82xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla82xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla81xx_nvram_config,
	.update_fw_options	= qla24xx_update_fw_options,
	.load_risc		= qla82xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla8044_intr_handler,
	.enable_intrs		= qla82xx_enable_intrs,
	.disable_intrs		= qla82xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= NULL,
	.write_nvram		= NULL,
	.fw_dump		= qla8044_fw_dump,
	.beacon_on		= qla82xx_beacon_on,
	.beacon_off		= qla82xx_beacon_off,
	.beacon_blink		= NULL,
	.read_optrom		= qla8044_read_optrom_data,
	.write_optrom		= qla8044_write_optrom_data,
	.get_flash_version	= qla82xx_get_flash_version,
	.start_scsi             = qla82xx_start_scsi,
	.start_scsi_mq          = NULL,
	.abort_isp		= qla8044_abort_isp,
	.iospace_config		= qla82xx_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qla83xx_isp_ops = {
	.pci_config		= qla25xx_pci_config,
	.reset_chip		= qla24xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla24xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla81xx_nvram_config,
	.update_fw_options	= qla81xx_update_fw_options,
	.load_risc		= qla81xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla24xx_intr_handler,
	.enable_intrs		= qla24xx_enable_intrs,
	.disable_intrs		= qla24xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= NULL,
	.write_nvram		= NULL,
	.fw_dump		= qla83xx_fw_dump,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= qla83xx_beacon_blink,
	.read_optrom		= qla25xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
	.start_scsi		= qla24xx_dif_start_scsi,
	.start_scsi_mq          = qla2xxx_dif_start_scsi_mq,
	.abort_isp		= qla2x00_abort_isp,
	.iospace_config		= qla83xx_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static struct isp_operations qlafx00_isp_ops = {
	.pci_config		= qlafx00_pci_config,
	.reset_chip		= qlafx00_soft_reset,
	.chip_diag		= qlafx00_chip_diag,
	.config_rings		= qlafx00_config_rings,
	.reset_adapter		= qlafx00_soft_reset,
	.nvram_config		= NULL,
	.update_fw_options	= NULL,
	.load_risc		= NULL,
	.pci_info_str		= qlafx00_pci_info_str,
	.fw_version_str		= qlafx00_fw_version_str,
	.intr_handler		= qlafx00_intr_handler,
	.enable_intrs		= qlafx00_enable_intrs,
	.disable_intrs		= qlafx00_disable_intrs,
	.abort_command		= qla24xx_async_abort_command,
	.target_reset		= qlafx00_abort_target,
	.lun_reset		= qlafx00_lun_reset,
	.fabric_login		= NULL,
	.fabric_logout		= NULL,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= qla24xx_read_nvram_data,
	.write_nvram		= qla24xx_write_nvram_data,
	.fw_dump		= NULL,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= NULL,
	.read_optrom		= qla24xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
	.start_scsi		= qlafx00_start_scsi,
	.start_scsi_mq          = NULL,
	.abort_isp		= qlafx00_abort_isp,
	.iospace_config		= qlafx00_iospace_config,
	.initialize_adapter	= qlafx00_initialize_adapter,
};

static struct isp_operations qla27xx_isp_ops = {
	.pci_config		= qla25xx_pci_config,
	.reset_chip		= qla24xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla24xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla81xx_nvram_config,
	.update_fw_options	= qla81xx_update_fw_options,
	.load_risc		= qla81xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla24xx_intr_handler,
	.enable_intrs		= qla24xx_enable_intrs,
	.disable_intrs		= qla24xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= NULL,
	.write_nvram		= NULL,
	.fw_dump		= qla27xx_fwdump,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= qla83xx_beacon_blink,
	.read_optrom		= qla25xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
	.start_scsi		= qla24xx_dif_start_scsi,
	.start_scsi_mq          = qla2xxx_dif_start_scsi_mq,
	.abort_isp		= qla2x00_abort_isp,
	.iospace_config		= qla83xx_iospace_config,
	.initialize_adapter	= qla2x00_initialize_adapter,
};

static inline void
qla2x00_set_isp_flags(struct qla_hw_data *ha)
{
	ha->device_type = DT_EXTENDED_IDS;
	switch (ha->pdev->device) {
	case PCI_DEVICE_ID_QLOGIC_ISP2100:
		ha->isp_type |= DT_ISP2100;
		ha->device_type &= ~DT_EXTENDED_IDS;
		ha->fw_srisc_address = RISC_START_ADDRESS_2100;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2200:
		ha->isp_type |= DT_ISP2200;
		ha->device_type &= ~DT_EXTENDED_IDS;
		ha->fw_srisc_address = RISC_START_ADDRESS_2100;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2300:
		ha->isp_type |= DT_ISP2300;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2312:
		ha->isp_type |= DT_ISP2312;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2322:
		ha->isp_type |= DT_ISP2322;
		ha->device_type |= DT_ZIO_SUPPORTED;
		if (ha->pdev->subsystem_vendor == 0x1028 &&
		    ha->pdev->subsystem_device == 0x0170)
			ha->device_type |= DT_OEM_001;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP6312:
		ha->isp_type |= DT_ISP6312;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP6322:
		ha->isp_type |= DT_ISP6322;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2422:
		ha->isp_type |= DT_ISP2422;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2432:
		ha->isp_type |= DT_ISP2432;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP8432:
		ha->isp_type |= DT_ISP8432;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP5422:
		ha->isp_type |= DT_ISP5422;
		ha->device_type |= DT_FWI2;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP5432:
		ha->isp_type |= DT_ISP5432;
		ha->device_type |= DT_FWI2;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2532:
		ha->isp_type |= DT_ISP2532;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP8001:
		ha->isp_type |= DT_ISP8001;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP8021:
		ha->isp_type |= DT_ISP8021;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		/* Initialize 82XX ISP flags */
		qla82xx_init_flags(ha);
		break;
	 case PCI_DEVICE_ID_QLOGIC_ISP8044:
		ha->isp_type |= DT_ISP8044;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		/* Initialize 82XX ISP flags */
		qla82xx_init_flags(ha);
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2031:
		ha->isp_type |= DT_ISP2031;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->device_type |= DT_T10_PI;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP8031:
		ha->isp_type |= DT_ISP8031;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->device_type |= DT_T10_PI;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISPF001:
		ha->isp_type |= DT_ISPFX00;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2071:
		ha->isp_type |= DT_ISP2071;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->device_type |= DT_T10_PI;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2271:
		ha->isp_type |= DT_ISP2271;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->device_type |= DT_T10_PI;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2261:
		ha->isp_type |= DT_ISP2261;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->device_type |= DT_T10_PI;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	}

	if (IS_QLA82XX(ha))
		ha->port_no = ha->portnum & 1;
	else {
		/* Get adapter physical port no from interrupt pin register. */
		pci_read_config_byte(ha->pdev, PCI_INTERRUPT_PIN, &ha->port_no);
		if (IS_QLA27XX(ha))
			ha->port_no--;
		else
			ha->port_no = !(ha->port_no & 1);
	}

	ql_dbg_pci(ql_dbg_init, ha->pdev, 0x000b,
	    "device_type=0x%x port=%d fw_srisc_address=0x%x.\n",
	    ha->device_type, ha->port_no, ha->fw_srisc_address);
}

static void
qla2xxx_scan_start(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	if (vha->hw->flags.running_gold_fw)
		return;

	set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
	set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
	set_bit(RSCN_UPDATE, &vha->dpc_flags);
	set_bit(NPIV_CONFIG_NEEDED, &vha->dpc_flags);
}

static int
qla2xxx_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	scsi_qla_host_t *vha = shost_priv(shost);

	if (test_bit(UNLOADING, &vha->dpc_flags))
		return 1;
	if (!vha->host)
		return 1;
	if (time > vha->hw->loop_reset_delay * HZ)
		return 1;

	return atomic_read(&vha->loop_state) == LOOP_READY;
}

static void qla2x00_iocb_work_fn(struct work_struct *work)
{
	struct scsi_qla_host *vha = container_of(work,
		struct scsi_qla_host, iocb_work);
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	int i = 20;
	unsigned long flags;

	if (test_bit(UNLOADING, &base_vha->dpc_flags))
		return;

	while (!list_empty(&vha->work_list) && i > 0) {
		qla2x00_do_work(vha);
		i--;
	}

	spin_lock_irqsave(&vha->work_lock, flags);
	clear_bit(IOCB_WORK_ACTIVE, &vha->dpc_flags);
	spin_unlock_irqrestore(&vha->work_lock, flags);
}

/*
 * PCI driver interface
 */
static int
qla2x00_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int	ret = -ENODEV;
	struct Scsi_Host *host;
	scsi_qla_host_t *base_vha = NULL;
	struct qla_hw_data *ha;
	char pci_info[30];
	char fw_str[30], wq_name[30];
	struct scsi_host_template *sht;
	int bars, mem_only = 0;
	uint16_t req_length = 0, rsp_length = 0;
	struct req_que *req = NULL;
	struct rsp_que *rsp = NULL;
	int i;

	bars = pci_select_bars(pdev, IORESOURCE_MEM | IORESOURCE_IO);
	sht = &qla2xxx_driver_template;
	if (pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2422 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2432 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8432 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP5422 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP5432 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2532 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8001 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8021 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2031 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8031 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISPF001 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8044 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2071 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2271 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2261) {
		bars = pci_select_bars(pdev, IORESOURCE_MEM);
		mem_only = 1;
		ql_dbg_pci(ql_dbg_init, pdev, 0x0007,
		    "Mem only adapter.\n");
	}
	ql_dbg_pci(ql_dbg_init, pdev, 0x0008,
	    "Bars=%d.\n", bars);

	if (mem_only) {
		if (pci_enable_device_mem(pdev))
			return ret;
	} else {
		if (pci_enable_device(pdev))
			return ret;
	}

	/* This may fail but that's ok */
	pci_enable_pcie_error_reporting(pdev);

	ha = kzalloc(sizeof(struct qla_hw_data), GFP_KERNEL);
	if (!ha) {
		ql_log_pci(ql_log_fatal, pdev, 0x0009,
		    "Unable to allocate memory for ha.\n");
		goto disable_device;
	}
	ql_dbg_pci(ql_dbg_init, pdev, 0x000a,
	    "Memory allocated for ha=%p.\n", ha);
	ha->pdev = pdev;
	INIT_LIST_HEAD(&ha->tgt.q_full_list);
	spin_lock_init(&ha->tgt.q_full_lock);
	spin_lock_init(&ha->tgt.sess_lock);
	spin_lock_init(&ha->tgt.atio_lock);

	atomic_set(&ha->nvme_active_aen_cnt, 0);

	/* Clear our data area */
	ha->bars = bars;
	ha->mem_only = mem_only;
	spin_lock_init(&ha->hardware_lock);
	spin_lock_init(&ha->vport_slock);
	mutex_init(&ha->selflogin_lock);
	mutex_init(&ha->optrom_mutex);

	/* Set ISP-type information. */
	qla2x00_set_isp_flags(ha);

	/* Set EEH reset type to fundamental if required by hba */
	if (IS_QLA24XX(ha) || IS_QLA25XX(ha) || IS_QLA81XX(ha) ||
	    IS_QLA83XX(ha) || IS_QLA27XX(ha))
		pdev->needs_freset = 1;

	ha->prev_topology = 0;
	ha->init_cb_size = sizeof(init_cb_t);
	ha->link_data_rate = PORT_SPEED_UNKNOWN;
	ha->optrom_size = OPTROM_SIZE_2300;
	ha->max_exchg = FW_MAX_EXCHANGES_CNT;
	atomic_set(&ha->num_pend_mbx_stage1, 0);
	atomic_set(&ha->num_pend_mbx_stage2, 0);
	atomic_set(&ha->num_pend_mbx_stage3, 0);

	/* Assign ISP specific operations. */
	if (IS_QLA2100(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2100;
		ha->mbx_count = MAILBOX_REGISTER_COUNT_2100;
		req_length = REQUEST_ENTRY_CNT_2100;
		rsp_length = RESPONSE_ENTRY_CNT_2100;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2100;
		ha->gid_list_info_size = 4;
		ha->flash_conf_off = ~0;
		ha->flash_data_off = ~0;
		ha->nvram_conf_off = ~0;
		ha->nvram_data_off = ~0;
		ha->isp_ops = &qla2100_isp_ops;
	} else if (IS_QLA2200(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2100;
		ha->mbx_count = MAILBOX_REGISTER_COUNT_2200;
		req_length = REQUEST_ENTRY_CNT_2200;
		rsp_length = RESPONSE_ENTRY_CNT_2100;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2100;
		ha->gid_list_info_size = 4;
		ha->flash_conf_off = ~0;
		ha->flash_data_off = ~0;
		ha->nvram_conf_off = ~0;
		ha->nvram_data_off = ~0;
		ha->isp_ops = &qla2100_isp_ops;
	} else if (IS_QLA23XX(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2100;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_2200;
		rsp_length = RESPONSE_ENTRY_CNT_2300;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->gid_list_info_size = 6;
		if (IS_QLA2322(ha) || IS_QLA6322(ha))
			ha->optrom_size = OPTROM_SIZE_2322;
		ha->flash_conf_off = ~0;
		ha->flash_data_off = ~0;
		ha->nvram_conf_off = ~0;
		ha->nvram_data_off = ~0;
		ha->isp_ops = &qla2300_isp_ops;
	} else if (IS_QLA24XX_TYPE(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_24XX;
		rsp_length = RESPONSE_ENTRY_CNT_2300;
		ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_24xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_24XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA24XX;
		ha->isp_ops = &qla24xx_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA;
		ha->nvram_conf_off = FARX_ACCESS_NVRAM_CONF;
		ha->nvram_data_off = FARX_ACCESS_NVRAM_DATA;
	} else if (IS_QLA25XX(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_24XX;
		rsp_length = RESPONSE_ENTRY_CNT_2300;
		ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_24xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_25XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA25XX;
		ha->isp_ops = &qla25xx_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA;
		ha->nvram_conf_off = FARX_ACCESS_NVRAM_CONF;
		ha->nvram_data_off = FARX_ACCESS_NVRAM_DATA;
	} else if (IS_QLA81XX(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_24XX;
		rsp_length = RESPONSE_ENTRY_CNT_2300;
		ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_81xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_81XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA25XX;
		ha->isp_ops = &qla81xx_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF_81XX;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA_81XX;
		ha->nvram_conf_off = ~0;
		ha->nvram_data_off = ~0;
	} else if (IS_QLA82XX(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_82XX;
		rsp_length = RESPONSE_ENTRY_CNT_82XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_81xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_82XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA25XX;
		ha->isp_ops = &qla82xx_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA;
		ha->nvram_conf_off = FARX_ACCESS_NVRAM_CONF;
		ha->nvram_data_off = FARX_ACCESS_NVRAM_DATA;
	} else if (IS_QLA8044(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_82XX;
		rsp_length = RESPONSE_ENTRY_CNT_82XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_81xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_83XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA25XX;
		ha->isp_ops = &qla8044_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA;
		ha->nvram_conf_off = FARX_ACCESS_NVRAM_CONF;
		ha->nvram_data_off = FARX_ACCESS_NVRAM_DATA;
	} else if (IS_QLA83XX(ha)) {
		ha->portnum = PCI_FUNC(ha->pdev->devfn);
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_83XX;
		rsp_length = RESPONSE_ENTRY_CNT_83XX;
		ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_81xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_83XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA25XX;
		ha->isp_ops = &qla83xx_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF_81XX;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA_81XX;
		ha->nvram_conf_off = ~0;
		ha->nvram_data_off = ~0;
	}  else if (IS_QLAFX00(ha)) {
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_FX00;
		ha->mbx_count = MAILBOX_REGISTER_COUNT_FX00;
		ha->aen_mbx_count = AEN_MAILBOX_REGISTER_COUNT_FX00;
		req_length = REQUEST_ENTRY_CNT_FX00;
		rsp_length = RESPONSE_ENTRY_CNT_FX00;
		ha->isp_ops = &qlafx00_isp_ops;
		ha->port_down_retry_count = 30; /* default value */
		ha->mr.fw_hbt_cnt = QLAFX00_HEARTBEAT_INTERVAL;
		ha->mr.fw_reset_timer_tick = QLAFX00_RESET_INTERVAL;
		ha->mr.fw_critemp_timer_tick = QLAFX00_CRITEMP_INTERVAL;
		ha->mr.fw_hbt_en = 1;
		ha->mr.host_info_resend = false;
		ha->mr.hinfo_resend_timer_tick = QLAFX00_HINFO_RESEND_INTERVAL;
	} else if (IS_QLA27XX(ha)) {
		ha->portnum = PCI_FUNC(ha->pdev->devfn);
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_83XX;
		rsp_length = RESPONSE_ENTRY_CNT_83XX;
		ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_81xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_83XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA25XX;
		ha->isp_ops = &qla27xx_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF_81XX;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA_81XX;
		ha->nvram_conf_off = ~0;
		ha->nvram_data_off = ~0;
	}

	ql_dbg_pci(ql_dbg_init, pdev, 0x001e,
	    "mbx_count=%d, req_length=%d, "
	    "rsp_length=%d, max_loop_id=%d, init_cb_size=%d, "
	    "gid_list_info_size=%d, optrom_size=%d, nvram_npiv_size=%d, "
	    "max_fibre_devices=%d.\n",
	    ha->mbx_count, req_length, rsp_length, ha->max_loop_id,
	    ha->init_cb_size, ha->gid_list_info_size, ha->optrom_size,
	    ha->nvram_npiv_size, ha->max_fibre_devices);
	ql_dbg_pci(ql_dbg_init, pdev, 0x001f,
	    "isp_ops=%p, flash_conf_off=%d, "
	    "flash_data_off=%d, nvram_conf_off=%d, nvram_data_off=%d.\n",
	    ha->isp_ops, ha->flash_conf_off, ha->flash_data_off,
	    ha->nvram_conf_off, ha->nvram_data_off);

	/* Configure PCI I/O space */
	ret = ha->isp_ops->iospace_config(ha);
	if (ret)
		goto iospace_config_failed;

	ql_log_pci(ql_log_info, pdev, 0x001d,
	    "Found an ISP%04X irq %d iobase 0x%p.\n",
	    pdev->device, pdev->irq, ha->iobase);
	mutex_init(&ha->vport_lock);
	mutex_init(&ha->mq_lock);
	init_completion(&ha->mbx_cmd_comp);
	complete(&ha->mbx_cmd_comp);
	init_completion(&ha->mbx_intr_comp);
	init_completion(&ha->dcbx_comp);
	init_completion(&ha->lb_portup_comp);

	set_bit(0, (unsigned long *) ha->vp_idx_map);

	qla2x00_config_dma_addressing(ha);
	ql_dbg_pci(ql_dbg_init, pdev, 0x0020,
	    "64 Bit addressing is %s.\n",
	    ha->flags.enable_64bit_addressing ? "enable" :
	    "disable");
	ret = qla2x00_mem_alloc(ha, req_length, rsp_length, &req, &rsp);
	if (ret) {
		ql_log_pci(ql_log_fatal, pdev, 0x0031,
		    "Failed to allocate memory for adapter, aborting.\n");

		goto probe_hw_failed;
	}

	req->max_q_depth = MAX_Q_DEPTH;
	if (ql2xmaxqdepth != 0 && ql2xmaxqdepth <= 0xffffU)
		req->max_q_depth = ql2xmaxqdepth;


	base_vha = qla2x00_create_host(sht, ha);
	if (!base_vha) {
		ret = -ENOMEM;
		goto probe_hw_failed;
	}

	pci_set_drvdata(pdev, base_vha);
	set_bit(PFLG_DRIVER_PROBING, &base_vha->pci_flags);

	host = base_vha->host;
	base_vha->req = req;
	if (IS_QLA2XXX_MIDTYPE(ha))
		base_vha->mgmt_svr_loop_id =
			qla2x00_reserve_mgmt_server_loop_id(base_vha);
	else
		base_vha->mgmt_svr_loop_id = MANAGEMENT_SERVER +
						base_vha->vp_idx;

	/* Setup fcport template structure. */
	ha->mr.fcport.vha = base_vha;
	ha->mr.fcport.port_type = FCT_UNKNOWN;
	ha->mr.fcport.loop_id = FC_NO_LOOP_ID;
	qla2x00_set_fcport_state(&ha->mr.fcport, FCS_UNCONFIGURED);
	ha->mr.fcport.supported_classes = FC_COS_UNSPECIFIED;
	ha->mr.fcport.scan_state = 1;

	/* Set the SG table size based on ISP type */
	if (!IS_FWI2_CAPABLE(ha)) {
		if (IS_QLA2100(ha))
			host->sg_tablesize = 32;
	} else {
		if (!IS_QLA82XX(ha))
			host->sg_tablesize = QLA_SG_ALL;
	}
	host->max_id = ha->max_fibre_devices;
	host->cmd_per_lun = 3;
	host->unique_id = host->host_no;
	if (IS_T10_PI_CAPABLE(ha) && ql2xenabledif)
		host->max_cmd_len = 32;
	else
		host->max_cmd_len = MAX_CMDSZ;
	host->max_channel = MAX_BUSES - 1;
	/* Older HBAs support only 16-bit LUNs */
	if (!IS_QLAFX00(ha) && !IS_FWI2_CAPABLE(ha) &&
	    ql2xmaxlun > 0xffff)
		host->max_lun = 0xffff;
	else
		host->max_lun = ql2xmaxlun;
	host->transportt = qla2xxx_transport_template;
	sht->vendor_id = (SCSI_NL_VID_TYPE_PCI | PCI_VENDOR_ID_QLOGIC);

	ql_dbg(ql_dbg_init, base_vha, 0x0033,
	    "max_id=%d this_id=%d "
	    "cmd_per_len=%d unique_id=%d max_cmd_len=%d max_channel=%d "
	    "max_lun=%llu transportt=%p, vendor_id=%llu.\n", host->max_id,
	    host->this_id, host->cmd_per_lun, host->unique_id,
	    host->max_cmd_len, host->max_channel, host->max_lun,
	    host->transportt, sht->vendor_id);

	INIT_WORK(&base_vha->iocb_work, qla2x00_iocb_work_fn);

	/* Set up the irqs */
	ret = qla2x00_request_irqs(ha, rsp);
	if (ret)
		goto probe_failed;

	/* Alloc arrays of request and response ring ptrs */
	ret = qla2x00_alloc_queues(ha, req, rsp);
	if (ret) {
		ql_log(ql_log_fatal, base_vha, 0x003d,
		    "Failed to allocate memory for queue pointers..."
		    "aborting.\n");
		goto probe_failed;
	}

	if (ha->mqenable && shost_use_blk_mq(host)) {
		/* number of hardware queues supported by blk/scsi-mq*/
		host->nr_hw_queues = ha->max_qpairs;

		ql_dbg(ql_dbg_init, base_vha, 0x0192,
			"blk/scsi-mq enabled, HW queues = %d.\n", host->nr_hw_queues);
	} else {
		if (ql2xnvmeenable) {
			host->nr_hw_queues = ha->max_qpairs;
			ql_dbg(ql_dbg_init, base_vha, 0x0194,
			    "FC-NVMe support is enabled, HW queues=%d\n",
			    host->nr_hw_queues);
		} else {
			ql_dbg(ql_dbg_init, base_vha, 0x0193,
			    "blk/scsi-mq disabled.\n");
		}
	}

	qlt_probe_one_stage1(base_vha, ha);

	pci_save_state(pdev);

	/* Assign back pointers */
	rsp->req = req;
	req->rsp = rsp;

	if (IS_QLAFX00(ha)) {
		ha->rsp_q_map[0] = rsp;
		ha->req_q_map[0] = req;
		set_bit(0, ha->req_qid_map);
		set_bit(0, ha->rsp_qid_map);
	}

	/* FWI2-capable only. */
	req->req_q_in = &ha->iobase->isp24.req_q_in;
	req->req_q_out = &ha->iobase->isp24.req_q_out;
	rsp->rsp_q_in = &ha->iobase->isp24.rsp_q_in;
	rsp->rsp_q_out = &ha->iobase->isp24.rsp_q_out;
	if (ha->mqenable || IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
		req->req_q_in = &ha->mqiobase->isp25mq.req_q_in;
		req->req_q_out = &ha->mqiobase->isp25mq.req_q_out;
		rsp->rsp_q_in = &ha->mqiobase->isp25mq.rsp_q_in;
		rsp->rsp_q_out =  &ha->mqiobase->isp25mq.rsp_q_out;
	}

	if (IS_QLAFX00(ha)) {
		req->req_q_in = &ha->iobase->ispfx00.req_q_in;
		req->req_q_out = &ha->iobase->ispfx00.req_q_out;
		rsp->rsp_q_in = &ha->iobase->ispfx00.rsp_q_in;
		rsp->rsp_q_out = &ha->iobase->ispfx00.rsp_q_out;
	}

	if (IS_P3P_TYPE(ha)) {
		req->req_q_out = &ha->iobase->isp82.req_q_out[0];
		rsp->rsp_q_in = &ha->iobase->isp82.rsp_q_in[0];
		rsp->rsp_q_out = &ha->iobase->isp82.rsp_q_out[0];
	}

	ql_dbg(ql_dbg_multiq, base_vha, 0xc009,
	    "rsp_q_map=%p req_q_map=%p rsp->req=%p req->rsp=%p.\n",
	    ha->rsp_q_map, ha->req_q_map, rsp->req, req->rsp);
	ql_dbg(ql_dbg_multiq, base_vha, 0xc00a,
	    "req->req_q_in=%p req->req_q_out=%p "
	    "rsp->rsp_q_in=%p rsp->rsp_q_out=%p.\n",
	    req->req_q_in, req->req_q_out,
	    rsp->rsp_q_in, rsp->rsp_q_out);
	ql_dbg(ql_dbg_init, base_vha, 0x003e,
	    "rsp_q_map=%p req_q_map=%p rsp->req=%p req->rsp=%p.\n",
	    ha->rsp_q_map, ha->req_q_map, rsp->req, req->rsp);
	ql_dbg(ql_dbg_init, base_vha, 0x003f,
	    "req->req_q_in=%p req->req_q_out=%p rsp->rsp_q_in=%p rsp->rsp_q_out=%p.\n",
	    req->req_q_in, req->req_q_out, rsp->rsp_q_in, rsp->rsp_q_out);

	ha->wq = alloc_workqueue("qla2xxx_wq", 0, 0);
	if (unlikely(!ha->wq)) {
		ret = -ENOMEM;
		goto probe_failed;
	}

	if (ha->isp_ops->initialize_adapter(base_vha)) {
		ql_log(ql_log_fatal, base_vha, 0x00d6,
		    "Failed to initialize adapter - Adapter flags %x.\n",
		    base_vha->device_flags);

		if (IS_QLA82XX(ha)) {
			qla82xx_idc_lock(ha);
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
				QLA8XXX_DEV_FAILED);
			qla82xx_idc_unlock(ha);
			ql_log(ql_log_fatal, base_vha, 0x00d7,
			    "HW State: FAILED.\n");
		} else if (IS_QLA8044(ha)) {
			qla8044_idc_lock(ha);
			qla8044_wr_direct(base_vha,
				QLA8044_CRB_DEV_STATE_INDEX,
				QLA8XXX_DEV_FAILED);
			qla8044_idc_unlock(ha);
			ql_log(ql_log_fatal, base_vha, 0x0150,
			    "HW State: FAILED.\n");
		}

		ret = -ENODEV;
		goto probe_failed;
	}

	if (IS_QLAFX00(ha))
		host->can_queue = QLAFX00_MAX_CANQUEUE;
	else
		host->can_queue = req->num_outstanding_cmds - 10;

	ql_dbg(ql_dbg_init, base_vha, 0x0032,
	    "can_queue=%d, req=%p, mgmt_svr_loop_id=%d, sg_tablesize=%d.\n",
	    host->can_queue, base_vha->req,
	    base_vha->mgmt_svr_loop_id, host->sg_tablesize);

	if (ha->mqenable) {
		bool mq = false;
		bool startit = false;

		if (QLA_TGT_MODE_ENABLED()) {
			mq = true;
			startit = false;
		}

		if ((ql2x_ini_mode == QLA2XXX_INI_MODE_ENABLED) &&
		    shost_use_blk_mq(host)) {
			mq = true;
			startit = true;
		}

		if (mq) {
			/* Create start of day qpairs for Block MQ */
			for (i = 0; i < ha->max_qpairs; i++)
				qla2xxx_create_qpair(base_vha, 5, 0, startit);
		}
	}

	if (ha->flags.running_gold_fw)
		goto skip_dpc;

	/*
	 * Startup the kernel thread for this host adapter
	 */
	ha->dpc_thread = kthread_create(qla2x00_do_dpc, ha,
	    "%s_dpc", base_vha->host_str);
	if (IS_ERR(ha->dpc_thread)) {
		ql_log(ql_log_fatal, base_vha, 0x00ed,
		    "Failed to start DPC thread.\n");
		ret = PTR_ERR(ha->dpc_thread);
		ha->dpc_thread = NULL;
		goto probe_failed;
	}
	ql_dbg(ql_dbg_init, base_vha, 0x00ee,
	    "DPC thread started successfully.\n");

	/*
	 * If we're not coming up in initiator mode, we might sit for
	 * a while without waking up the dpc thread, which leads to a
	 * stuck process warning.  So just kick the dpc once here and
	 * let the kthread start (and go back to sleep in qla2x00_do_dpc).
	 */
	qla2xxx_wake_dpc(base_vha);

	INIT_WORK(&ha->board_disable, qla2x00_disable_board_on_pci_error);

	if (IS_QLA8031(ha) || IS_MCTP_CAPABLE(ha)) {
		sprintf(wq_name, "qla2xxx_%lu_dpc_lp_wq", base_vha->host_no);
		ha->dpc_lp_wq = create_singlethread_workqueue(wq_name);
		INIT_WORK(&ha->idc_aen, qla83xx_service_idc_aen);

		sprintf(wq_name, "qla2xxx_%lu_dpc_hp_wq", base_vha->host_no);
		ha->dpc_hp_wq = create_singlethread_workqueue(wq_name);
		INIT_WORK(&ha->nic_core_reset, qla83xx_nic_core_reset_work);
		INIT_WORK(&ha->idc_state_handler,
		    qla83xx_idc_state_handler_work);
		INIT_WORK(&ha->nic_core_unrecoverable,
		    qla83xx_nic_core_unrecoverable_work);
	}

skip_dpc:
	list_add_tail(&base_vha->list, &ha->vp_list);
	base_vha->host->irq = ha->pdev->irq;

	/* Initialized the timer */
	qla2x00_start_timer(base_vha, WATCH_INTERVAL);
	ql_dbg(ql_dbg_init, base_vha, 0x00ef,
	    "Started qla2x00_timer with "
	    "interval=%d.\n", WATCH_INTERVAL);
	ql_dbg(ql_dbg_init, base_vha, 0x00f0,
	    "Detected hba at address=%p.\n",
	    ha);

	if (IS_T10_PI_CAPABLE(ha) && ql2xenabledif) {
		if (ha->fw_attributes & BIT_4) {
			int prot = 0, guard;
			base_vha->flags.difdix_supported = 1;
			ql_dbg(ql_dbg_init, base_vha, 0x00f1,
			    "Registering for DIF/DIX type 1 and 3 protection.\n");
			if (ql2xenabledif == 1)
				prot = SHOST_DIX_TYPE0_PROTECTION;
			scsi_host_set_prot(host,
			    prot | SHOST_DIF_TYPE1_PROTECTION
			    | SHOST_DIF_TYPE2_PROTECTION
			    | SHOST_DIF_TYPE3_PROTECTION
			    | SHOST_DIX_TYPE1_PROTECTION
			    | SHOST_DIX_TYPE2_PROTECTION
			    | SHOST_DIX_TYPE3_PROTECTION);

			guard = SHOST_DIX_GUARD_CRC;

			if (IS_PI_IPGUARD_CAPABLE(ha) &&
			    (ql2xenabledif > 1 || IS_PI_DIFB_DIX0_CAPABLE(ha)))
				guard |= SHOST_DIX_GUARD_IP;

			scsi_host_set_guard(host, guard);
		} else
			base_vha->flags.difdix_supported = 0;
	}

	ha->isp_ops->enable_intrs(ha);

	if (IS_QLAFX00(ha)) {
		ret = qlafx00_fx_disc(base_vha,
			&base_vha->hw->mr.fcport, FXDISC_GET_CONFIG_INFO);
		host->sg_tablesize = (ha->mr.extended_io_enabled) ?
		    QLA_SG_ALL : 128;
	}

	ret = scsi_add_host(host, &pdev->dev);
	if (ret)
		goto probe_failed;

	base_vha->flags.init_done = 1;
	base_vha->flags.online = 1;
	ha->prev_minidump_failed = 0;

	ql_dbg(ql_dbg_init, base_vha, 0x00f2,
	    "Init done and hba is online.\n");

	if (qla_ini_mode_enabled(base_vha) ||
		qla_dual_mode_enabled(base_vha))
		scsi_scan_host(host);
	else
		ql_dbg(ql_dbg_init, base_vha, 0x0122,
			"skipping scsi_scan_host() for non-initiator port\n");

	qla2x00_alloc_sysfs_attr(base_vha);

	if (IS_QLAFX00(ha)) {
		ret = qlafx00_fx_disc(base_vha,
			&base_vha->hw->mr.fcport, FXDISC_GET_PORT_INFO);

		/* Register system information */
		ret =  qlafx00_fx_disc(base_vha,
			&base_vha->hw->mr.fcport, FXDISC_REG_HOST_INFO);
	}

	qla2x00_init_host_attr(base_vha);

	qla2x00_dfs_setup(base_vha);

	ql_log(ql_log_info, base_vha, 0x00fb,
	    "QLogic %s - %s.\n", ha->model_number, ha->model_desc);
	ql_log(ql_log_info, base_vha, 0x00fc,
	    "ISP%04X: %s @ %s hdma%c host#=%ld fw=%s.\n",
	    pdev->device, ha->isp_ops->pci_info_str(base_vha, pci_info),
	    pci_name(pdev), ha->flags.enable_64bit_addressing ? '+' : '-',
	    base_vha->host_no,
	    ha->isp_ops->fw_version_str(base_vha, fw_str, sizeof(fw_str)));

	qlt_add_target(ha, base_vha);

	clear_bit(PFLG_DRIVER_PROBING, &base_vha->pci_flags);

	if (test_bit(UNLOADING, &base_vha->dpc_flags))
		return -ENODEV;

	if (ha->flags.detected_lr_sfp) {
		ql_log(ql_log_info, base_vha, 0xffff,
		    "Reset chip to pick up LR SFP setting\n");
		set_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags);
		qla2xxx_wake_dpc(base_vha);
	}

	return 0;

probe_failed:
	if (base_vha->gnl.l) {
		dma_free_coherent(&ha->pdev->dev, base_vha->gnl.size,
				base_vha->gnl.l, base_vha->gnl.ldma);
		base_vha->gnl.l = NULL;
	}

	if (base_vha->timer_active)
		qla2x00_stop_timer(base_vha);
	base_vha->flags.online = 0;
	if (ha->dpc_thread) {
		struct task_struct *t = ha->dpc_thread;

		ha->dpc_thread = NULL;
		kthread_stop(t);
	}

	qla2x00_free_device(base_vha);
	scsi_host_put(base_vha->host);
	/*
	 * Need to NULL out local req/rsp after
	 * qla2x00_free_device => qla2x00_free_queues frees
	 * what these are pointing to. Or else we'll
	 * fall over below in qla2x00_free_req/rsp_que.
	 */
	req = NULL;
	rsp = NULL;

probe_hw_failed:
	qla2x00_mem_free(ha);
	qla2x00_free_req_que(ha, req);
	qla2x00_free_rsp_que(ha, rsp);
	qla2x00_clear_drv_active(ha);

iospace_config_failed:
	if (IS_P3P_TYPE(ha)) {
		if (!ha->nx_pcibase)
			iounmap((device_reg_t *)ha->nx_pcibase);
		if (!ql2xdbwr)
			iounmap((device_reg_t *)ha->nxdb_wr_ptr);
	} else {
		if (ha->iobase)
			iounmap(ha->iobase);
		if (ha->cregbase)
			iounmap(ha->cregbase);
	}
	pci_release_selected_regions(ha->pdev, ha->bars);
	kfree(ha);

disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void
qla2x00_shutdown(struct pci_dev *pdev)
{
	scsi_qla_host_t *vha;
	struct qla_hw_data  *ha;

	vha = pci_get_drvdata(pdev);
	ha = vha->hw;

	ql_log(ql_log_info, vha, 0xfffa,
		"Adapter shutdown\n");

	/*
	 * Prevent future board_disable and wait
	 * until any pending board_disable has completed.
	 */
	set_bit(PFLG_DRIVER_REMOVING, &vha->pci_flags);
	cancel_work_sync(&ha->board_disable);

	if (!atomic_read(&pdev->enable_cnt))
		return;

	/* Notify ISPFX00 firmware */
	if (IS_QLAFX00(ha))
		qlafx00_driver_shutdown(vha, 20);

	/* Turn-off FCE trace */
	if (ha->flags.fce_enabled) {
		qla2x00_disable_fce_trace(vha, NULL, NULL);
		ha->flags.fce_enabled = 0;
	}

	/* Turn-off EFT trace */
	if (ha->eft)
		qla2x00_disable_eft_trace(vha);

	if (IS_QLA25XX(ha) ||  IS_QLA2031(ha) || IS_QLA27XX(ha)) {
		if (ha->flags.fw_started)
			qla2x00_abort_isp_cleanup(vha);
	} else {
		/* Stop currently executing firmware. */
		qla2x00_try_to_stop_firmware(vha);
	}

	/* Disable timer */
	if (vha->timer_active)
		qla2x00_stop_timer(vha);

	/* Turn adapter off line */
	vha->flags.online = 0;

	/* turn-off interrupts on the card */
	if (ha->interrupts_on) {
		vha->flags.init_done = 0;
		ha->isp_ops->disable_intrs(ha);
	}

	qla2x00_free_irqs(vha);

	qla2x00_free_fw_dump(ha);

	pci_disable_device(pdev);
	ql_log(ql_log_info, vha, 0xfffe,
		"Adapter shutdown successfully.\n");
}

/* Deletes all the virtual ports for a given ha */
static void
qla2x00_delete_all_vps(struct qla_hw_data *ha, scsi_qla_host_t *base_vha)
{
	scsi_qla_host_t *vha;
	unsigned long flags;

	mutex_lock(&ha->vport_lock);
	while (ha->cur_vport_count) {
		spin_lock_irqsave(&ha->vport_slock, flags);

		BUG_ON(base_vha->list.next == &ha->vp_list);
		/* This assumes first entry in ha->vp_list is always base vha */
		vha = list_first_entry(&base_vha->list, scsi_qla_host_t, list);
		scsi_host_get(vha->host);

		spin_unlock_irqrestore(&ha->vport_slock, flags);
		mutex_unlock(&ha->vport_lock);

		qla_nvme_delete(vha);

		fc_vport_terminate(vha->fc_vport);
		scsi_host_put(vha->host);

		mutex_lock(&ha->vport_lock);
	}
	mutex_unlock(&ha->vport_lock);
}

/* Stops all deferred work threads */
static void
qla2x00_destroy_deferred_work(struct qla_hw_data *ha)
{
	/* Cancel all work and destroy DPC workqueues */
	if (ha->dpc_lp_wq) {
		cancel_work_sync(&ha->idc_aen);
		destroy_workqueue(ha->dpc_lp_wq);
		ha->dpc_lp_wq = NULL;
	}

	if (ha->dpc_hp_wq) {
		cancel_work_sync(&ha->nic_core_reset);
		cancel_work_sync(&ha->idc_state_handler);
		cancel_work_sync(&ha->nic_core_unrecoverable);
		destroy_workqueue(ha->dpc_hp_wq);
		ha->dpc_hp_wq = NULL;
	}

	/* Kill the kernel thread for this host */
	if (ha->dpc_thread) {
		struct task_struct *t = ha->dpc_thread;

		/*
		 * qla2xxx_wake_dpc checks for ->dpc_thread
		 * so we need to zero it out.
		 */
		ha->dpc_thread = NULL;
		kthread_stop(t);
	}
}

static void
qla2x00_unmap_iobases(struct qla_hw_data *ha)
{
	if (IS_QLA82XX(ha)) {

		iounmap((device_reg_t *)ha->nx_pcibase);
		if (!ql2xdbwr)
			iounmap((device_reg_t *)ha->nxdb_wr_ptr);
	} else {
		if (ha->iobase)
			iounmap(ha->iobase);

		if (ha->cregbase)
			iounmap(ha->cregbase);

		if (ha->mqiobase)
			iounmap(ha->mqiobase);

		if ((IS_QLA83XX(ha) || IS_QLA27XX(ha)) && ha->msixbase)
			iounmap(ha->msixbase);
	}
}

static void
qla2x00_clear_drv_active(struct qla_hw_data *ha)
{
	if (IS_QLA8044(ha)) {
		qla8044_idc_lock(ha);
		qla8044_clear_drv_active(ha);
		qla8044_idc_unlock(ha);
	} else if (IS_QLA82XX(ha)) {
		qla82xx_idc_lock(ha);
		qla82xx_clear_drv_active(ha);
		qla82xx_idc_unlock(ha);
	}
}

static void
qla2x00_remove_one(struct pci_dev *pdev)
{
	scsi_qla_host_t *base_vha;
	struct qla_hw_data  *ha;

	base_vha = pci_get_drvdata(pdev);
	ha = base_vha->hw;
	ql_log(ql_log_info, base_vha, 0xb079,
	    "Removing driver\n");

	/* Indicate device removal to prevent future board_disable and wait
	 * until any pending board_disable has completed. */
	set_bit(PFLG_DRIVER_REMOVING, &base_vha->pci_flags);
	cancel_work_sync(&ha->board_disable);

	/*
	 * If the PCI device is disabled then there was a PCI-disconnect and
	 * qla2x00_disable_board_on_pci_error has taken care of most of the
	 * resources.
	 */
	if (!atomic_read(&pdev->enable_cnt)) {
		dma_free_coherent(&ha->pdev->dev, base_vha->gnl.size,
		    base_vha->gnl.l, base_vha->gnl.ldma);
		base_vha->gnl.l = NULL;
		scsi_host_put(base_vha->host);
		kfree(ha);
		pci_set_drvdata(pdev, NULL);
		return;
	}
	qla2x00_wait_for_hba_ready(base_vha);

	if (IS_QLA25XX(ha) || IS_QLA2031(ha) || IS_QLA27XX(ha)) {
		if (ha->flags.fw_started)
			qla2x00_abort_isp_cleanup(base_vha);
	} else if (!IS_QLAFX00(ha)) {
		if (IS_QLA8031(ha)) {
			ql_dbg(ql_dbg_p3p, base_vha, 0xb07e,
			    "Clearing fcoe driver presence.\n");
			if (qla83xx_clear_drv_presence(base_vha) != QLA_SUCCESS)
				ql_dbg(ql_dbg_p3p, base_vha, 0xb079,
				    "Error while clearing DRV-Presence.\n");
		}

		qla2x00_try_to_stop_firmware(base_vha);
	}

	qla2x00_wait_for_sess_deletion(base_vha);

	/*
	 * if UNLOAD flag is already set, then continue unload,
	 * where it was set first.
	 */
	if (test_bit(UNLOADING, &base_vha->dpc_flags))
		return;

	set_bit(UNLOADING, &base_vha->dpc_flags);

	qla_nvme_delete(base_vha);

	dma_free_coherent(&ha->pdev->dev,
		base_vha->gnl.size, base_vha->gnl.l, base_vha->gnl.ldma);

	base_vha->gnl.l = NULL;

	vfree(base_vha->scan.l);

	if (IS_QLAFX00(ha))
		qlafx00_driver_shutdown(base_vha, 20);

	qla2x00_delete_all_vps(ha, base_vha);

	qla2x00_abort_all_cmds(base_vha, DID_NO_CONNECT << 16);

	qla2x00_dfs_remove(base_vha);

	qla84xx_put_chip(base_vha);

	/* Disable timer */
	if (base_vha->timer_active)
		qla2x00_stop_timer(base_vha);

	base_vha->flags.online = 0;

	/* free DMA memory */
	if (ha->exlogin_buf)
		qla2x00_free_exlogin_buffer(ha);

	/* free DMA memory */
	if (ha->exchoffld_buf)
		qla2x00_free_exchoffld_buffer(ha);

	qla2x00_destroy_deferred_work(ha);

	qlt_remove_target(ha, base_vha);

	qla2x00_free_sysfs_attr(base_vha, true);

	fc_remove_host(base_vha->host);
	qlt_remove_target_resources(ha);

	scsi_remove_host(base_vha->host);

	qla2x00_free_device(base_vha);

	qla2x00_clear_drv_active(ha);

	scsi_host_put(base_vha->host);

	qla2x00_unmap_iobases(ha);

	pci_release_selected_regions(ha->pdev, ha->bars);
	kfree(ha);

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

static void
qla2x00_free_device(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	qla2x00_abort_all_cmds(vha, DID_NO_CONNECT << 16);

	/* Disable timer */
	if (vha->timer_active)
		qla2x00_stop_timer(vha);

	qla25xx_delete_queues(vha);
	vha->flags.online = 0;

	/* turn-off interrupts on the card */
	if (ha->interrupts_on) {
		vha->flags.init_done = 0;
		ha->isp_ops->disable_intrs(ha);
	}

	qla2x00_free_fcports(vha);

	qla2x00_free_irqs(vha);

	/* Flush the work queue and remove it */
	if (ha->wq) {
		flush_workqueue(ha->wq);
		destroy_workqueue(ha->wq);
		ha->wq = NULL;
	}


	qla2x00_mem_free(ha);

	qla82xx_md_free(vha);

	qla2x00_free_queues(ha);
}

void qla2x00_free_fcports(struct scsi_qla_host *vha)
{
	fc_port_t *fcport, *tfcport;

	list_for_each_entry_safe(fcport, tfcport, &vha->vp_fcports, list) {
		list_del(&fcport->list);
		qla2x00_clear_loop_id(fcport);
		kfree(fcport);
	}
}

static inline void
qla2x00_schedule_rport_del(struct scsi_qla_host *vha, fc_port_t *fcport,
    int defer)
{
	struct fc_rport *rport;
	scsi_qla_host_t *base_vha;
	unsigned long flags;

	if (!fcport->rport)
		return;

	rport = fcport->rport;
	if (defer) {
		base_vha = pci_get_drvdata(vha->hw->pdev);
		spin_lock_irqsave(vha->host->host_lock, flags);
		fcport->drport = rport;
		spin_unlock_irqrestore(vha->host->host_lock, flags);
		qlt_do_generation_tick(vha, &base_vha->total_fcport_update_gen);
		set_bit(FCPORT_UPDATE_NEEDED, &base_vha->dpc_flags);
		qla2xxx_wake_dpc(base_vha);
	} else {
		int now;
		if (rport) {
			ql_dbg(ql_dbg_disc, fcport->vha, 0x2109,
			    "%s %8phN. rport %p roles %x\n",
			    __func__, fcport->port_name, rport,
			    rport->roles);
			fc_remote_port_delete(rport);
		}
		qlt_do_generation_tick(vha, &now);
	}
}

/*
 * qla2x00_mark_device_lost Updates fcport state when device goes offline.
 *
 * Input: ha = adapter block pointer.  fcport = port structure pointer.
 *
 * Return: None.
 *
 * Context:
 */
void qla2x00_mark_device_lost(scsi_qla_host_t *vha, fc_port_t *fcport,
    int do_login, int defer)
{
	if (IS_QLAFX00(vha->hw)) {
		qla2x00_set_fcport_state(fcport, FCS_DEVICE_LOST);
		qla2x00_schedule_rport_del(vha, fcport, defer);
		return;
	}

	if (atomic_read(&fcport->state) == FCS_ONLINE &&
	    vha->vp_idx == fcport->vha->vp_idx) {
		qla2x00_set_fcport_state(fcport, FCS_DEVICE_LOST);
		qla2x00_schedule_rport_del(vha, fcport, defer);
	}
	/*
	 * We may need to retry the login, so don't change the state of the
	 * port but do the retries.
	 */
	if (atomic_read(&fcport->state) != FCS_DEVICE_DEAD)
		qla2x00_set_fcport_state(fcport, FCS_DEVICE_LOST);

	if (!do_login)
		return;

	set_bit(RELOGIN_NEEDED, &vha->dpc_flags);
}

/*
 * qla2x00_mark_all_devices_lost
 *	Updates fcport state when device goes offline.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	None.
 *
 * Context:
 */
void
qla2x00_mark_all_devices_lost(scsi_qla_host_t *vha, int defer)
{
	fc_port_t *fcport;

	ql_dbg(ql_dbg_disc, vha, 0x20f1,
	    "Mark all dev lost\n");

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		fcport->scan_state = 0;
		qlt_schedule_sess_for_deletion(fcport);

		if (vha->vp_idx != 0 && vha->vp_idx != fcport->vha->vp_idx)
			continue;

		/*
		 * No point in marking the device as lost, if the device is
		 * already DEAD.
		 */
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD)
			continue;
		if (atomic_read(&fcport->state) == FCS_ONLINE) {
			qla2x00_set_fcport_state(fcport, FCS_DEVICE_LOST);
			if (defer)
				qla2x00_schedule_rport_del(vha, fcport, defer);
			else if (vha->vp_idx == fcport->vha->vp_idx)
				qla2x00_schedule_rport_del(vha, fcport, defer);
		}
	}
}

/*
* qla2x00_mem_alloc
*      Allocates adapter memory.
*
* Returns:
*      0  = success.
*      !0  = failure.
*/
static int
qla2x00_mem_alloc(struct qla_hw_data *ha, uint16_t req_len, uint16_t rsp_len,
	struct req_que **req, struct rsp_que **rsp)
{
	char	name[16];

	ha->init_cb = dma_alloc_coherent(&ha->pdev->dev, ha->init_cb_size,
		&ha->init_cb_dma, GFP_KERNEL);
	if (!ha->init_cb)
		goto fail;

	if (qlt_mem_alloc(ha) < 0)
		goto fail_free_init_cb;

	ha->gid_list = dma_alloc_coherent(&ha->pdev->dev,
		qla2x00_gid_list_size(ha), &ha->gid_list_dma, GFP_KERNEL);
	if (!ha->gid_list)
		goto fail_free_tgt_mem;

	ha->srb_mempool = mempool_create_slab_pool(SRB_MIN_REQ, srb_cachep);
	if (!ha->srb_mempool)
		goto fail_free_gid_list;

	if (IS_P3P_TYPE(ha)) {
		/* Allocate cache for CT6 Ctx. */
		if (!ctx_cachep) {
			ctx_cachep = kmem_cache_create("qla2xxx_ctx",
				sizeof(struct ct6_dsd), 0,
				SLAB_HWCACHE_ALIGN, NULL);
			if (!ctx_cachep)
				goto fail_free_srb_mempool;
		}
		ha->ctx_mempool = mempool_create_slab_pool(SRB_MIN_REQ,
			ctx_cachep);
		if (!ha->ctx_mempool)
			goto fail_free_srb_mempool;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0021,
		    "ctx_cachep=%p ctx_mempool=%p.\n",
		    ctx_cachep, ha->ctx_mempool);
	}

	/* Get memory for cached NVRAM */
	ha->nvram = kzalloc(MAX_NVRAM_SIZE, GFP_KERNEL);
	if (!ha->nvram)
		goto fail_free_ctx_mempool;

	snprintf(name, sizeof(name), "%s_%d", QLA2XXX_DRIVER_NAME,
		ha->pdev->device);
	ha->s_dma_pool = dma_pool_create(name, &ha->pdev->dev,
		DMA_POOL_SIZE, 8, 0);
	if (!ha->s_dma_pool)
		goto fail_free_nvram;

	ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0022,
	    "init_cb=%p gid_list=%p, srb_mempool=%p s_dma_pool=%p.\n",
	    ha->init_cb, ha->gid_list, ha->srb_mempool, ha->s_dma_pool);

	if (IS_P3P_TYPE(ha) || ql2xenabledif) {
		ha->dl_dma_pool = dma_pool_create(name, &ha->pdev->dev,
			DSD_LIST_DMA_POOL_SIZE, 8, 0);
		if (!ha->dl_dma_pool) {
			ql_log_pci(ql_log_fatal, ha->pdev, 0x0023,
			    "Failed to allocate memory for dl_dma_pool.\n");
			goto fail_s_dma_pool;
		}

		ha->fcp_cmnd_dma_pool = dma_pool_create(name, &ha->pdev->dev,
			FCP_CMND_DMA_POOL_SIZE, 8, 0);
		if (!ha->fcp_cmnd_dma_pool) {
			ql_log_pci(ql_log_fatal, ha->pdev, 0x0024,
			    "Failed to allocate memory for fcp_cmnd_dma_pool.\n");
			goto fail_dl_dma_pool;
		}
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0025,
		    "dl_dma_pool=%p fcp_cmnd_dma_pool=%p.\n",
		    ha->dl_dma_pool, ha->fcp_cmnd_dma_pool);
	}

	/* Allocate memory for SNS commands */
	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
	/* Get consistent memory allocated for SNS commands */
		ha->sns_cmd = dma_alloc_coherent(&ha->pdev->dev,
		sizeof(struct sns_cmd_pkt), &ha->sns_cmd_dma, GFP_KERNEL);
		if (!ha->sns_cmd)
			goto fail_dma_pool;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0026,
		    "sns_cmd: %p.\n", ha->sns_cmd);
	} else {
	/* Get consistent memory allocated for MS IOCB */
		ha->ms_iocb = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
			&ha->ms_iocb_dma);
		if (!ha->ms_iocb)
			goto fail_dma_pool;
	/* Get consistent memory allocated for CT SNS commands */
		ha->ct_sns = dma_alloc_coherent(&ha->pdev->dev,
			sizeof(struct ct_sns_pkt), &ha->ct_sns_dma, GFP_KERNEL);
		if (!ha->ct_sns)
			goto fail_free_ms_iocb;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0027,
		    "ms_iocb=%p ct_sns=%p.\n",
		    ha->ms_iocb, ha->ct_sns);
	}

	/* Allocate memory for request ring */
	*req = kzalloc(sizeof(struct req_que), GFP_KERNEL);
	if (!*req) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0028,
		    "Failed to allocate memory for req.\n");
		goto fail_req;
	}
	(*req)->length = req_len;
	(*req)->ring = dma_alloc_coherent(&ha->pdev->dev,
		((*req)->length + 1) * sizeof(request_t),
		&(*req)->dma, GFP_KERNEL);
	if (!(*req)->ring) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0029,
		    "Failed to allocate memory for req_ring.\n");
		goto fail_req_ring;
	}
	/* Allocate memory for response ring */
	*rsp = kzalloc(sizeof(struct rsp_que), GFP_KERNEL);
	if (!*rsp) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x002a,
		    "Failed to allocate memory for rsp.\n");
		goto fail_rsp;
	}
	(*rsp)->hw = ha;
	(*rsp)->length = rsp_len;
	(*rsp)->ring = dma_alloc_coherent(&ha->pdev->dev,
		((*rsp)->length + 1) * sizeof(response_t),
		&(*rsp)->dma, GFP_KERNEL);
	if (!(*rsp)->ring) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x002b,
		    "Failed to allocate memory for rsp_ring.\n");
		goto fail_rsp_ring;
	}
	(*req)->rsp = *rsp;
	(*rsp)->req = *req;
	ql_dbg_pci(ql_dbg_init, ha->pdev, 0x002c,
	    "req=%p req->length=%d req->ring=%p rsp=%p "
	    "rsp->length=%d rsp->ring=%p.\n",
	    *req, (*req)->length, (*req)->ring, *rsp, (*rsp)->length,
	    (*rsp)->ring);
	/* Allocate memory for NVRAM data for vports */
	if (ha->nvram_npiv_size) {
		ha->npiv_info = kcalloc(ha->nvram_npiv_size,
					sizeof(struct qla_npiv_entry),
					GFP_KERNEL);
		if (!ha->npiv_info) {
			ql_log_pci(ql_log_fatal, ha->pdev, 0x002d,
			    "Failed to allocate memory for npiv_info.\n");
			goto fail_npiv_info;
		}
	} else
		ha->npiv_info = NULL;

	/* Get consistent memory allocated for EX-INIT-CB. */
	if (IS_CNA_CAPABLE(ha) || IS_QLA2031(ha) || IS_QLA27XX(ha)) {
		ha->ex_init_cb = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
		    &ha->ex_init_cb_dma);
		if (!ha->ex_init_cb)
			goto fail_ex_init_cb;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x002e,
		    "ex_init_cb=%p.\n", ha->ex_init_cb);
	}

	INIT_LIST_HEAD(&ha->gbl_dsd_list);

	/* Get consistent memory allocated for Async Port-Database. */
	if (!IS_FWI2_CAPABLE(ha)) {
		ha->async_pd = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
			&ha->async_pd_dma);
		if (!ha->async_pd)
			goto fail_async_pd;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x002f,
		    "async_pd=%p.\n", ha->async_pd);
	}

	INIT_LIST_HEAD(&ha->vp_list);

	/* Allocate memory for our loop_id bitmap */
	ha->loop_id_map = kcalloc(BITS_TO_LONGS(LOOPID_MAP_SIZE),
				  sizeof(long),
				  GFP_KERNEL);
	if (!ha->loop_id_map)
		goto fail_loop_id_map;
	else {
		qla2x00_set_reserved_loop_ids(ha);
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0123,
		    "loop_id_map=%p.\n", ha->loop_id_map);
	}

	ha->sfp_data = dma_alloc_coherent(&ha->pdev->dev,
	    SFP_DEV_SIZE, &ha->sfp_data_dma, GFP_KERNEL);
	if (!ha->sfp_data) {
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x011b,
		    "Unable to allocate memory for SFP read-data.\n");
		goto fail_sfp_data;
	}

	return 0;

fail_sfp_data:
	kfree(ha->loop_id_map);
fail_loop_id_map:
	dma_pool_free(ha->s_dma_pool, ha->async_pd, ha->async_pd_dma);
fail_async_pd:
	dma_pool_free(ha->s_dma_pool, ha->ex_init_cb, ha->ex_init_cb_dma);
fail_ex_init_cb:
	kfree(ha->npiv_info);
fail_npiv_info:
	dma_free_coherent(&ha->pdev->dev, ((*rsp)->length + 1) *
		sizeof(response_t), (*rsp)->ring, (*rsp)->dma);
	(*rsp)->ring = NULL;
	(*rsp)->dma = 0;
fail_rsp_ring:
	kfree(*rsp);
	*rsp = NULL;
fail_rsp:
	dma_free_coherent(&ha->pdev->dev, ((*req)->length + 1) *
		sizeof(request_t), (*req)->ring, (*req)->dma);
	(*req)->ring = NULL;
	(*req)->dma = 0;
fail_req_ring:
	kfree(*req);
	*req = NULL;
fail_req:
	dma_free_coherent(&ha->pdev->dev, sizeof(struct ct_sns_pkt),
		ha->ct_sns, ha->ct_sns_dma);
	ha->ct_sns = NULL;
	ha->ct_sns_dma = 0;
fail_free_ms_iocb:
	dma_pool_free(ha->s_dma_pool, ha->ms_iocb, ha->ms_iocb_dma);
	ha->ms_iocb = NULL;
	ha->ms_iocb_dma = 0;

	if (ha->sns_cmd)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct sns_cmd_pkt),
		    ha->sns_cmd, ha->sns_cmd_dma);
fail_dma_pool:
	if (IS_QLA82XX(ha) || ql2xenabledif) {
		dma_pool_destroy(ha->fcp_cmnd_dma_pool);
		ha->fcp_cmnd_dma_pool = NULL;
	}
fail_dl_dma_pool:
	if (IS_QLA82XX(ha) || ql2xenabledif) {
		dma_pool_destroy(ha->dl_dma_pool);
		ha->dl_dma_pool = NULL;
	}
fail_s_dma_pool:
	dma_pool_destroy(ha->s_dma_pool);
	ha->s_dma_pool = NULL;
fail_free_nvram:
	kfree(ha->nvram);
	ha->nvram = NULL;
fail_free_ctx_mempool:
	if (ha->ctx_mempool)
		mempool_destroy(ha->ctx_mempool);
	ha->ctx_mempool = NULL;
fail_free_srb_mempool:
	if (ha->srb_mempool)
		mempool_destroy(ha->srb_mempool);
	ha->srb_mempool = NULL;
fail_free_gid_list:
	dma_free_coherent(&ha->pdev->dev, qla2x00_gid_list_size(ha),
	ha->gid_list,
	ha->gid_list_dma);
	ha->gid_list = NULL;
	ha->gid_list_dma = 0;
fail_free_tgt_mem:
	qlt_mem_free(ha);
fail_free_init_cb:
	dma_free_coherent(&ha->pdev->dev, ha->init_cb_size, ha->init_cb,
	ha->init_cb_dma);
	ha->init_cb = NULL;
	ha->init_cb_dma = 0;
fail:
	ql_log(ql_log_fatal, NULL, 0x0030,
	    "Memory allocation failure.\n");
	return -ENOMEM;
}

int
qla2x00_set_exlogins_buffer(scsi_qla_host_t *vha)
{
	int rval;
	uint16_t	size, max_cnt, temp;
	struct qla_hw_data *ha = vha->hw;

	/* Return if we don't need to alloacate any extended logins */
	if (!ql2xexlogins)
		return QLA_SUCCESS;

	if (!IS_EXLOGIN_OFFLD_CAPABLE(ha))
		return QLA_SUCCESS;

	ql_log(ql_log_info, vha, 0xd021, "EXLOGIN count: %d.\n", ql2xexlogins);
	max_cnt = 0;
	rval = qla_get_exlogin_status(vha, &size, &max_cnt);
	if (rval != QLA_SUCCESS) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0xd029,
		    "Failed to get exlogin status.\n");
		return rval;
	}

	temp = (ql2xexlogins > max_cnt) ? max_cnt : ql2xexlogins;
	temp *= size;

	if (temp != ha->exlogin_size) {
		qla2x00_free_exlogin_buffer(ha);
		ha->exlogin_size = temp;

		ql_log(ql_log_info, vha, 0xd024,
		    "EXLOGIN: max_logins=%d, portdb=0x%x, total=%d.\n",
		    max_cnt, size, temp);

		ql_log(ql_log_info, vha, 0xd025,
		    "EXLOGIN: requested size=0x%x\n", ha->exlogin_size);

		/* Get consistent memory for extended logins */
		ha->exlogin_buf = dma_alloc_coherent(&ha->pdev->dev,
			ha->exlogin_size, &ha->exlogin_buf_dma, GFP_KERNEL);
		if (!ha->exlogin_buf) {
			ql_log_pci(ql_log_fatal, ha->pdev, 0xd02a,
		    "Failed to allocate memory for exlogin_buf_dma.\n");
			return -ENOMEM;
		}
	}

	/* Now configure the dma buffer */
	rval = qla_set_exlogin_mem_cfg(vha, ha->exlogin_buf_dma);
	if (rval) {
		ql_log(ql_log_fatal, vha, 0xd033,
		    "Setup extended login buffer  ****FAILED****.\n");
		qla2x00_free_exlogin_buffer(ha);
	}

	return rval;
}

/*
* qla2x00_free_exlogin_buffer
*
* Input:
*	ha = adapter block pointer
*/
void
qla2x00_free_exlogin_buffer(struct qla_hw_data *ha)
{
	if (ha->exlogin_buf) {
		dma_free_coherent(&ha->pdev->dev, ha->exlogin_size,
		    ha->exlogin_buf, ha->exlogin_buf_dma);
		ha->exlogin_buf = NULL;
		ha->exlogin_size = 0;
	}
}

static void
qla2x00_number_of_exch(scsi_qla_host_t *vha, u32 *ret_cnt, u16 max_cnt)
{
	u32 temp;
	*ret_cnt = FW_DEF_EXCHANGES_CNT;

	if (max_cnt > vha->hw->max_exchg)
		max_cnt = vha->hw->max_exchg;

	if (qla_ini_mode_enabled(vha)) {
		if (ql2xiniexchg > max_cnt)
			ql2xiniexchg = max_cnt;

		if (ql2xiniexchg > FW_DEF_EXCHANGES_CNT)
			*ret_cnt = ql2xiniexchg;
	} else if (qla_tgt_mode_enabled(vha)) {
		if (ql2xexchoffld > max_cnt)
			ql2xexchoffld = max_cnt;

		if (ql2xexchoffld > FW_DEF_EXCHANGES_CNT)
			*ret_cnt = ql2xexchoffld;
	} else if (qla_dual_mode_enabled(vha)) {
		temp = ql2xiniexchg + ql2xexchoffld;
		if (temp > max_cnt) {
			ql2xiniexchg -= (temp - max_cnt)/2;
			ql2xexchoffld -= (((temp - max_cnt)/2) + 1);
			temp = max_cnt;
		}

		if (temp > FW_DEF_EXCHANGES_CNT)
			*ret_cnt = temp;
	}
}

int
qla2x00_set_exchoffld_buffer(scsi_qla_host_t *vha)
{
	int rval;
	u16	size, max_cnt;
	u32 actual_cnt, totsz;
	struct qla_hw_data *ha = vha->hw;

	if (!ha->flags.exchoffld_enabled)
		return QLA_SUCCESS;

	if (!IS_EXCHG_OFFLD_CAPABLE(ha))
		return QLA_SUCCESS;

	max_cnt = 0;
	rval = qla_get_exchoffld_status(vha, &size, &max_cnt);
	if (rval != QLA_SUCCESS) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0xd012,
		    "Failed to get exlogin status.\n");
		return rval;
	}

	qla2x00_number_of_exch(vha, &actual_cnt, max_cnt);
	ql_log(ql_log_info, vha, 0xd014,
	    "Actual exchange offload count: %d.\n", actual_cnt);

	totsz = actual_cnt * size;

	if (totsz != ha->exchoffld_size) {
		qla2x00_free_exchoffld_buffer(ha);
		ha->exchoffld_size = totsz;

		ql_log(ql_log_info, vha, 0xd016,
		    "Exchange offload: max_count=%d, actual count=%d entry sz=0x%x, total sz=0x%x\n",
		    max_cnt, actual_cnt, size, totsz);

		ql_log(ql_log_info, vha, 0xd017,
		    "Exchange Buffers requested size = 0x%x\n",
		    ha->exchoffld_size);

		/* Get consistent memory for extended logins */
		ha->exchoffld_buf = dma_alloc_coherent(&ha->pdev->dev,
			ha->exchoffld_size, &ha->exchoffld_buf_dma, GFP_KERNEL);
		if (!ha->exchoffld_buf) {
			ql_log_pci(ql_log_fatal, ha->pdev, 0xd013,
			"Failed to allocate memory for Exchange Offload.\n");

			if (ha->max_exchg >
			    (FW_DEF_EXCHANGES_CNT + REDUCE_EXCHANGES_CNT)) {
				ha->max_exchg -= REDUCE_EXCHANGES_CNT;
			} else if (ha->max_exchg >
			    (FW_DEF_EXCHANGES_CNT + 512)) {
				ha->max_exchg -= 512;
			} else {
				ha->flags.exchoffld_enabled = 0;
				ql_log_pci(ql_log_fatal, ha->pdev, 0xd013,
				    "Disabling Exchange offload due to lack of memory\n");
			}
			ha->exchoffld_size = 0;

			return -ENOMEM;
		}
	}

	/* Now configure the dma buffer */
	rval = qla_set_exchoffld_mem_cfg(vha);
	if (rval) {
		ql_log(ql_log_fatal, vha, 0xd02e,
		    "Setup exchange offload buffer ****FAILED****.\n");
		qla2x00_free_exchoffld_buffer(ha);
	} else {
		/* re-adjust number of target exchange */
		struct init_cb_81xx *icb = (struct init_cb_81xx *)ha->init_cb;

		if (qla_ini_mode_enabled(vha))
			icb->exchange_count = 0;
		else
			icb->exchange_count = cpu_to_le16(ql2xexchoffld);
	}

	return rval;
}

/*
* qla2x00_free_exchoffld_buffer
*
* Input:
*	ha = adapter block pointer
*/
void
qla2x00_free_exchoffld_buffer(struct qla_hw_data *ha)
{
	if (ha->exchoffld_buf) {
		dma_free_coherent(&ha->pdev->dev, ha->exchoffld_size,
		    ha->exchoffld_buf, ha->exchoffld_buf_dma);
		ha->exchoffld_buf = NULL;
		ha->exchoffld_size = 0;
	}
}

/*
* qla2x00_free_fw_dump
*	Frees fw dump stuff.
*
* Input:
*	ha = adapter block pointer
*/
static void
qla2x00_free_fw_dump(struct qla_hw_data *ha)
{
	if (ha->fce)
		dma_free_coherent(&ha->pdev->dev,
		    FCE_SIZE, ha->fce, ha->fce_dma);

	if (ha->eft)
		dma_free_coherent(&ha->pdev->dev,
		    EFT_SIZE, ha->eft, ha->eft_dma);

	if (ha->fw_dump)
		vfree(ha->fw_dump);
	if (ha->fw_dump_template)
		vfree(ha->fw_dump_template);

	ha->fce = NULL;
	ha->fce_dma = 0;
	ha->eft = NULL;
	ha->eft_dma = 0;
	ha->fw_dumped = 0;
	ha->fw_dump_cap_flags = 0;
	ha->fw_dump_reading = 0;
	ha->fw_dump = NULL;
	ha->fw_dump_len = 0;
	ha->fw_dump_template = NULL;
	ha->fw_dump_template_len = 0;
}

/*
* qla2x00_mem_free
*      Frees all adapter allocated memory.
*
* Input:
*      ha = adapter block pointer.
*/
static void
qla2x00_mem_free(struct qla_hw_data *ha)
{
	qla2x00_free_fw_dump(ha);

	if (ha->mctp_dump)
		dma_free_coherent(&ha->pdev->dev, MCTP_DUMP_SIZE, ha->mctp_dump,
		    ha->mctp_dump_dma);

	if (ha->srb_mempool)
		mempool_destroy(ha->srb_mempool);

	if (ha->dcbx_tlv)
		dma_free_coherent(&ha->pdev->dev, DCBX_TLV_DATA_SIZE,
		    ha->dcbx_tlv, ha->dcbx_tlv_dma);

	if (ha->xgmac_data)
		dma_free_coherent(&ha->pdev->dev, XGMAC_DATA_SIZE,
		    ha->xgmac_data, ha->xgmac_data_dma);

	if (ha->sns_cmd)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct sns_cmd_pkt),
		ha->sns_cmd, ha->sns_cmd_dma);

	if (ha->ct_sns)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct ct_sns_pkt),
		ha->ct_sns, ha->ct_sns_dma);

	if (ha->sfp_data)
		dma_free_coherent(&ha->pdev->dev, SFP_DEV_SIZE, ha->sfp_data,
		    ha->sfp_data_dma);

	if (ha->ms_iocb)
		dma_pool_free(ha->s_dma_pool, ha->ms_iocb, ha->ms_iocb_dma);

	if (ha->ex_init_cb)
		dma_pool_free(ha->s_dma_pool,
			ha->ex_init_cb, ha->ex_init_cb_dma);

	if (ha->async_pd)
		dma_pool_free(ha->s_dma_pool, ha->async_pd, ha->async_pd_dma);

	if (ha->s_dma_pool)
		dma_pool_destroy(ha->s_dma_pool);

	if (ha->gid_list)
		dma_free_coherent(&ha->pdev->dev, qla2x00_gid_list_size(ha),
		ha->gid_list, ha->gid_list_dma);

	if (IS_QLA82XX(ha)) {
		if (!list_empty(&ha->gbl_dsd_list)) {
			struct dsd_dma *dsd_ptr, *tdsd_ptr;

			/* clean up allocated prev pool */
			list_for_each_entry_safe(dsd_ptr,
				tdsd_ptr, &ha->gbl_dsd_list, list) {
				dma_pool_free(ha->dl_dma_pool,
				dsd_ptr->dsd_addr, dsd_ptr->dsd_list_dma);
				list_del(&dsd_ptr->list);
				kfree(dsd_ptr);
			}
		}
	}

	if (ha->dl_dma_pool)
		dma_pool_destroy(ha->dl_dma_pool);

	if (ha->fcp_cmnd_dma_pool)
		dma_pool_destroy(ha->fcp_cmnd_dma_pool);

	if (ha->ctx_mempool)
		mempool_destroy(ha->ctx_mempool);

	qlt_mem_free(ha);

	if (ha->init_cb)
		dma_free_coherent(&ha->pdev->dev, ha->init_cb_size,
			ha->init_cb, ha->init_cb_dma);

	vfree(ha->optrom_buffer);
	kfree(ha->nvram);
	kfree(ha->npiv_info);
	kfree(ha->swl);
	kfree(ha->loop_id_map);

	ha->srb_mempool = NULL;
	ha->ctx_mempool = NULL;
	ha->sns_cmd = NULL;
	ha->sns_cmd_dma = 0;
	ha->ct_sns = NULL;
	ha->ct_sns_dma = 0;
	ha->ms_iocb = NULL;
	ha->ms_iocb_dma = 0;
	ha->init_cb = NULL;
	ha->init_cb_dma = 0;
	ha->ex_init_cb = NULL;
	ha->ex_init_cb_dma = 0;
	ha->async_pd = NULL;
	ha->async_pd_dma = 0;
	ha->loop_id_map = NULL;
	ha->npiv_info = NULL;
	ha->optrom_buffer = NULL;
	ha->swl = NULL;
	ha->nvram = NULL;
	ha->mctp_dump = NULL;
	ha->dcbx_tlv = NULL;
	ha->xgmac_data = NULL;
	ha->sfp_data = NULL;

	ha->s_dma_pool = NULL;
	ha->dl_dma_pool = NULL;
	ha->fcp_cmnd_dma_pool = NULL;

	ha->gid_list = NULL;
	ha->gid_list_dma = 0;

	ha->tgt.atio_ring = NULL;
	ha->tgt.atio_dma = 0;
	ha->tgt.tgt_vp_map = NULL;
}

struct scsi_qla_host *qla2x00_create_host(struct scsi_host_template *sht,
						struct qla_hw_data *ha)
{
	struct Scsi_Host *host;
	struct scsi_qla_host *vha = NULL;

	host = scsi_host_alloc(sht, sizeof(scsi_qla_host_t));
	if (!host) {
		ql_log_pci(ql_log_fatal, ha->pdev, 0x0107,
		    "Failed to allocate host from the scsi layer, aborting.\n");
		return NULL;
	}

	/* Clear our data area */
	vha = shost_priv(host);
	memset(vha, 0, sizeof(scsi_qla_host_t));

	vha->host = host;
	vha->host_no = host->host_no;
	vha->hw = ha;

	INIT_LIST_HEAD(&vha->vp_fcports);
	INIT_LIST_HEAD(&vha->work_list);
	INIT_LIST_HEAD(&vha->list);
	INIT_LIST_HEAD(&vha->qla_cmd_list);
	INIT_LIST_HEAD(&vha->qla_sess_op_cmd_list);
	INIT_LIST_HEAD(&vha->logo_list);
	INIT_LIST_HEAD(&vha->plogi_ack_list);
	INIT_LIST_HEAD(&vha->qp_list);
	INIT_LIST_HEAD(&vha->gnl.fcports);
	INIT_LIST_HEAD(&vha->nvme_rport_list);
	INIT_LIST_HEAD(&vha->gpnid_list);
	INIT_WORK(&vha->iocb_work, qla2x00_iocb_work_fn);

	spin_lock_init(&vha->work_lock);
	spin_lock_init(&vha->cmd_list_lock);
	spin_lock_init(&vha->gnl.fcports_lock);
	init_waitqueue_head(&vha->fcport_waitQ);
	init_waitqueue_head(&vha->vref_waitq);

	vha->gnl.size = sizeof(struct get_name_list_extended) *
			(ha->max_loop_id + 1);
	vha->gnl.l = dma_alloc_coherent(&ha->pdev->dev,
	    vha->gnl.size, &vha->gnl.ldma, GFP_KERNEL);
	if (!vha->gnl.l) {
		ql_log(ql_log_fatal, vha, 0xd04a,
		    "Alloc failed for name list.\n");
		scsi_remove_host(vha->host);
		return NULL;
	}

	/* todo: what about ext login? */
	vha->scan.size = ha->max_fibre_devices * sizeof(struct fab_scan_rp);
	vha->scan.l = vmalloc(vha->scan.size);
	if (!vha->scan.l) {
		ql_log(ql_log_fatal, vha, 0xd04a,
		    "Alloc failed for scan database.\n");
		dma_free_coherent(&ha->pdev->dev, vha->gnl.size,
		    vha->gnl.l, vha->gnl.ldma);
		vha->gnl.l = NULL;
		scsi_remove_host(vha->host);
		return NULL;
	}
	INIT_DELAYED_WORK(&vha->scan.scan_work, qla_scan_work_fn);

	sprintf(vha->host_str, "%s_%ld", QLA2XXX_DRIVER_NAME, vha->host_no);
	ql_dbg(ql_dbg_init, vha, 0x0041,
	    "Allocated the host=%p hw=%p vha=%p dev_name=%s",
	    vha->host, vha->hw, vha,
	    dev_name(&(ha->pdev->dev)));

	return vha;
}

struct qla_work_evt *
qla2x00_alloc_work(struct scsi_qla_host *vha, enum qla_work_type type)
{
	struct qla_work_evt *e;
	uint8_t bail;

	QLA_VHA_MARK_BUSY(vha, bail);
	if (bail)
		return NULL;

	e = kzalloc(sizeof(struct qla_work_evt), GFP_ATOMIC);
	if (!e) {
		QLA_VHA_MARK_NOT_BUSY(vha);
		return NULL;
	}

	INIT_LIST_HEAD(&e->list);
	e->type = type;
	e->flags = QLA_EVT_FLAG_FREE;
	return e;
}

int
qla2x00_post_work(struct scsi_qla_host *vha, struct qla_work_evt *e)
{
	unsigned long flags;
	bool q = false;

	spin_lock_irqsave(&vha->work_lock, flags);
	list_add_tail(&e->list, &vha->work_list);

	if (!test_and_set_bit(IOCB_WORK_ACTIVE, &vha->dpc_flags))
		q = true;

	spin_unlock_irqrestore(&vha->work_lock, flags);

	if (q)
		queue_work(vha->hw->wq, &vha->iocb_work);

	return QLA_SUCCESS;
}

int
qla2x00_post_aen_work(struct scsi_qla_host *vha, enum fc_host_event_code code,
    u32 data)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_AEN);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.aen.code = code;
	e->u.aen.data = data;
	return qla2x00_post_work(vha, e);
}

int
qla2x00_post_idc_ack_work(struct scsi_qla_host *vha, uint16_t *mb)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_IDC_ACK);
	if (!e)
		return QLA_FUNCTION_FAILED;

	memcpy(e->u.idc_ack.mb, mb, QLA_IDC_ACK_REGS * sizeof(uint16_t));
	return qla2x00_post_work(vha, e);
}

#define qla2x00_post_async_work(name, type)	\
int qla2x00_post_async_##name##_work(		\
    struct scsi_qla_host *vha,			\
    fc_port_t *fcport, uint16_t *data)		\
{						\
	struct qla_work_evt *e;			\
						\
	e = qla2x00_alloc_work(vha, type);	\
	if (!e)					\
		return QLA_FUNCTION_FAILED;	\
						\
	e->u.logio.fcport = fcport;		\
	if (data) {				\
		e->u.logio.data[0] = data[0];	\
		e->u.logio.data[1] = data[1];	\
	}					\
	fcport->flags |= FCF_ASYNC_ACTIVE;	\
	return qla2x00_post_work(vha, e);	\
}

qla2x00_post_async_work(login, QLA_EVT_ASYNC_LOGIN);
qla2x00_post_async_work(logout, QLA_EVT_ASYNC_LOGOUT);
qla2x00_post_async_work(logout_done, QLA_EVT_ASYNC_LOGOUT_DONE);
qla2x00_post_async_work(adisc, QLA_EVT_ASYNC_ADISC);
qla2x00_post_async_work(adisc_done, QLA_EVT_ASYNC_ADISC_DONE);
qla2x00_post_async_work(prlo, QLA_EVT_ASYNC_PRLO);
qla2x00_post_async_work(prlo_done, QLA_EVT_ASYNC_PRLO_DONE);

int
qla2x00_post_uevent_work(struct scsi_qla_host *vha, u32 code)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_UEVENT);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.uevent.code = code;
	return qla2x00_post_work(vha, e);
}

static void
qla2x00_uevent_emit(struct scsi_qla_host *vha, u32 code)
{
	char event_string[40];
	char *envp[] = { event_string, NULL };

	switch (code) {
	case QLA_UEVENT_CODE_FW_DUMP:
		snprintf(event_string, sizeof(event_string), "FW_DUMP=%ld",
		    vha->host_no);
		break;
	default:
		/* do nothing */
		break;
	}
	kobject_uevent_env(&vha->hw->pdev->dev.kobj, KOBJ_CHANGE, envp);
}

int
qlafx00_post_aenfx_work(struct scsi_qla_host *vha,  uint32_t evtcode,
			uint32_t *data, int cnt)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_AENFX);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.aenfx.evtcode = evtcode;
	e->u.aenfx.count = cnt;
	memcpy(e->u.aenfx.mbx, data, sizeof(*data) * cnt);
	return qla2x00_post_work(vha, e);
}

int qla24xx_post_upd_fcport_work(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_UPD_FCPORT);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.fcport.fcport = fcport;
	return qla2x00_post_work(vha, e);
}

static
void qla24xx_create_new_sess(struct scsi_qla_host *vha, struct qla_work_evt *e)
{
	unsigned long flags;
	fc_port_t *fcport =  NULL, *tfcp;
	struct qlt_plogi_ack_t *pla =
	    (struct qlt_plogi_ack_t *)e->u.new_sess.pla;
	uint8_t free_fcport = 0;

	ql_dbg(ql_dbg_disc, vha, 0xffff,
	    "%s %d %8phC enter\n",
	    __func__, __LINE__, e->u.new_sess.port_name);

	spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
	fcport = qla2x00_find_fcport_by_wwpn(vha, e->u.new_sess.port_name, 1);
	if (fcport) {
		fcport->d_id = e->u.new_sess.id;
		if (pla) {
			fcport->fw_login_state = DSC_LS_PLOGI_PEND;
			memcpy(fcport->node_name,
			    pla->iocb.u.isp24.u.plogi.node_name,
			    WWN_SIZE);
			qlt_plogi_ack_link(vha, pla, fcport, QLT_PLOGI_LINK_SAME_WWN);
			/* we took an extra ref_count to prevent PLOGI ACK when
			 * fcport/sess has not been created.
			 */
			pla->ref_count--;
		}
	} else {
		spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);
		fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
		if (fcport) {
			fcport->d_id = e->u.new_sess.id;
			fcport->flags |= FCF_FABRIC_DEVICE;
			fcport->fw_login_state = DSC_LS_PLOGI_PEND;
			if (e->u.new_sess.fc4_type == FS_FC4TYPE_FCP)
				fcport->fc4_type = FC4_TYPE_FCP_SCSI;

			if (e->u.new_sess.fc4_type == FS_FC4TYPE_NVME) {
				fcport->fc4_type = FC4_TYPE_OTHER;
				fcport->fc4f_nvme = FC4_TYPE_NVME;
			}

			memcpy(fcport->port_name, e->u.new_sess.port_name,
			    WWN_SIZE);
		} else {
			ql_dbg(ql_dbg_disc, vha, 0xffff,
				   "%s %8phC mem alloc fail.\n",
				   __func__, e->u.new_sess.port_name);

			if (pla)
				kmem_cache_free(qla_tgt_plogi_cachep, pla);
			return;
		}

		spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
		/* search again to make sure no one else got ahead */
		tfcp = qla2x00_find_fcport_by_wwpn(vha,
		    e->u.new_sess.port_name, 1);
		if (tfcp) {
			/* should rarily happen */
			ql_dbg(ql_dbg_disc, vha, 0xffff,
			    "%s %8phC found existing fcport b4 add. DS %d LS %d\n",
			    __func__, tfcp->port_name, tfcp->disc_state,
			    tfcp->fw_login_state);

			free_fcport = 1;
		} else {
			list_add_tail(&fcport->list, &vha->vp_fcports);

		}
		if (pla) {
			qlt_plogi_ack_link(vha, pla, fcport,
			    QLT_PLOGI_LINK_SAME_WWN);
			pla->ref_count--;
		}
	}
	spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);

	if (fcport) {
		fcport->id_changed = 1;
		fcport->scan_state = QLA_FCPORT_FOUND;
		fcport->chip_reset = vha->hw->base_qpair->chip_reset;
		memcpy(fcport->node_name, e->u.new_sess.node_name, WWN_SIZE);

		if (pla) {
			if (pla->iocb.u.isp24.status_subcode == ELS_PRLI) {
				u16 wd3_lo;

				fcport->fw_login_state = DSC_LS_PRLI_PEND;
				fcport->local = 0;
				fcport->loop_id =
					le16_to_cpu(
					    pla->iocb.u.isp24.nport_handle);
				fcport->fw_login_state = DSC_LS_PRLI_PEND;
				wd3_lo =
				    le16_to_cpu(
					pla->iocb.u.isp24.u.prli.wd3_lo);

				if (wd3_lo & BIT_7)
					fcport->conf_compl_supported = 1;

				if ((wd3_lo & BIT_4) == 0)
					fcport->port_type = FCT_INITIATOR;
				else
					fcport->port_type = FCT_TARGET;
			}
			qlt_plogi_ack_unref(vha, pla);
		} else {
			fc_port_t *dfcp = NULL;

			spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
			tfcp = qla2x00_find_fcport_by_nportid(vha,
			    &e->u.new_sess.id, 1);
			if (tfcp && (tfcp != fcport)) {
				/*
				 * We have a conflict fcport with same NportID.
				 */
				ql_dbg(ql_dbg_disc, vha, 0xffff,
				    "%s %8phC found conflict b4 add. DS %d LS %d\n",
				    __func__, tfcp->port_name, tfcp->disc_state,
				    tfcp->fw_login_state);

				switch (tfcp->disc_state) {
				case DSC_DELETED:
					break;
				case DSC_DELETE_PEND:
					fcport->login_pause = 1;
					tfcp->conflict = fcport;
					break;
				default:
					fcport->login_pause = 1;
					tfcp->conflict = fcport;
					dfcp = tfcp;
					break;
				}
			}
			spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);
			if (dfcp)
				qlt_schedule_sess_for_deletion(tfcp);


			if (N2N_TOPO(vha->hw))
				fcport->flags &= ~FCF_FABRIC_DEVICE;

			if (N2N_TOPO(vha->hw)) {
				if (vha->flags.nvme_enabled) {
					fcport->fc4f_nvme = 1;
					fcport->n2n_flag = 1;
				}
				fcport->fw_login_state = 0;
				/*
				 * wait link init done before sending login
				 */
			} else {
				qla24xx_fcport_handle_login(vha, fcport);
			}
		}
	}

	if (free_fcport) {
		qla2x00_free_fcport(fcport);
		if (pla)
			kmem_cache_free(qla_tgt_plogi_cachep, pla);
	}
}

static void qla_sp_retry(struct scsi_qla_host *vha, struct qla_work_evt *e)
{
	struct srb *sp = e->u.iosb.sp;
	int rval;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_disc, vha, 0x2043,
		    "%s: %s: Re-issue IOCB failed (%d).\n",
		    __func__, sp->name, rval);
		qla24xx_sp_unmap(vha, sp);
	}
}

void
qla2x00_do_work(struct scsi_qla_host *vha)
{
	struct qla_work_evt *e, *tmp;
	unsigned long flags;
	LIST_HEAD(work);

	spin_lock_irqsave(&vha->work_lock, flags);
	list_splice_init(&vha->work_list, &work);
	spin_unlock_irqrestore(&vha->work_lock, flags);

	list_for_each_entry_safe(e, tmp, &work, list) {
		list_del_init(&e->list);

		switch (e->type) {
		case QLA_EVT_AEN:
			fc_host_post_event(vha->host, fc_get_event_number(),
			    e->u.aen.code, e->u.aen.data);
			break;
		case QLA_EVT_IDC_ACK:
			qla81xx_idc_ack(vha, e->u.idc_ack.mb);
			break;
		case QLA_EVT_ASYNC_LOGIN:
			qla2x00_async_login(vha, e->u.logio.fcport,
			    e->u.logio.data);
			break;
		case QLA_EVT_ASYNC_LOGOUT:
			qla2x00_async_logout(vha, e->u.logio.fcport);
			break;
		case QLA_EVT_ASYNC_LOGOUT_DONE:
			qla2x00_async_logout_done(vha, e->u.logio.fcport,
			    e->u.logio.data);
			break;
		case QLA_EVT_ASYNC_ADISC:
			qla2x00_async_adisc(vha, e->u.logio.fcport,
			    e->u.logio.data);
			break;
		case QLA_EVT_ASYNC_ADISC_DONE:
			qla2x00_async_adisc_done(vha, e->u.logio.fcport,
			    e->u.logio.data);
			break;
		case QLA_EVT_UEVENT:
			qla2x00_uevent_emit(vha, e->u.uevent.code);
			break;
		case QLA_EVT_AENFX:
			qlafx00_process_aen(vha, e);
			break;
		case QLA_EVT_GIDPN:
			qla24xx_async_gidpn(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_GPNID:
			qla24xx_async_gpnid(vha, &e->u.gpnid.id);
			break;
		case QLA_EVT_UNMAP:
			qla24xx_sp_unmap(vha, e->u.iosb.sp);
			break;
		case QLA_EVT_RELOGIN:
			qla2x00_relogin(vha);
			break;
		case QLA_EVT_NEW_SESS:
			qla24xx_create_new_sess(vha, e);
			break;
		case QLA_EVT_GPDB:
			qla24xx_async_gpdb(vha, e->u.fcport.fcport,
			    e->u.fcport.opt);
			break;
		case QLA_EVT_PRLI:
			qla24xx_async_prli(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_GPSC:
			qla24xx_async_gpsc(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_UPD_FCPORT:
			qla2x00_update_fcport(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_GNL:
			qla24xx_async_gnl(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_NACK:
			qla24xx_do_nack_work(vha, e);
			break;
		case QLA_EVT_ASYNC_PRLO:
			qla2x00_async_prlo(vha, e->u.logio.fcport);
			break;
		case QLA_EVT_ASYNC_PRLO_DONE:
			qla2x00_async_prlo_done(vha, e->u.logio.fcport,
			    e->u.logio.data);
			break;
		case QLA_EVT_GPNFT:
			qla24xx_async_gpnft(vha, e->u.gpnft.fc4_type,
			    e->u.gpnft.sp);
			break;
		case QLA_EVT_GPNFT_DONE:
			qla24xx_async_gpnft_done(vha, e->u.iosb.sp);
			break;
		case QLA_EVT_GNNFT_DONE:
			qla24xx_async_gnnft_done(vha, e->u.iosb.sp);
			break;
		case QLA_EVT_GNNID:
			qla24xx_async_gnnid(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_GFPNID:
			qla24xx_async_gfpnid(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_SP_RETRY:
			qla_sp_retry(vha, e);
			break;
		case QLA_EVT_IIDMA:
			qla_do_iidma_work(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_ELS_PLOGI:
			qla24xx_els_dcmd2_iocb(vha, ELS_DCMD_PLOGI,
			    e->u.fcport.fcport, false);
			break;
		}
		if (e->flags & QLA_EVT_FLAG_FREE)
			kfree(e);

		/* For each work completed decrement vha ref count */
		QLA_VHA_MARK_NOT_BUSY(vha);
	}
}

int qla24xx_post_relogin_work(struct scsi_qla_host *vha)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_RELOGIN);

	if (!e) {
		set_bit(RELOGIN_NEEDED, &vha->dpc_flags);
		return QLA_FUNCTION_FAILED;
	}

	return qla2x00_post_work(vha, e);
}

/* Relogins all the fcports of a vport
 * Context: dpc thread
 */
void qla2x00_relogin(struct scsi_qla_host *vha)
{
	fc_port_t       *fcport;
	int status, relogin_needed = 0;
	struct event_arg ea;

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		/*
		 * If the port is not ONLINE then try to login
		 * to it if we haven't run out of retries.
		 */
		if (atomic_read(&fcport->state) != FCS_ONLINE &&
		    fcport->login_retry) {
			if (fcport->scan_state != QLA_FCPORT_FOUND ||
			    fcport->disc_state == DSC_LOGIN_COMPLETE)
				continue;

			if (fcport->flags & (FCF_ASYNC_SENT|FCF_ASYNC_ACTIVE) ||
				fcport->disc_state == DSC_DELETE_PEND) {
				relogin_needed = 1;
			} else {
				if (vha->hw->current_topology != ISP_CFG_NL) {
					memset(&ea, 0, sizeof(ea));
					ea.event = FCME_RELOGIN;
					ea.fcport = fcport;
					qla2x00_fcport_event_handler(vha, &ea);
				} else if (vha->hw->current_topology ==
				    ISP_CFG_NL) {
					fcport->login_retry--;
					status =
					    qla2x00_local_device_login(vha,
						fcport);
					if (status == QLA_SUCCESS) {
						fcport->old_loop_id =
						    fcport->loop_id;
						ql_dbg(ql_dbg_disc, vha, 0x2003,
						    "Port login OK: logged in ID 0x%x.\n",
						    fcport->loop_id);
						qla2x00_update_fcport
							(vha, fcport);
					} else if (status == 1) {
						set_bit(RELOGIN_NEEDED,
						    &vha->dpc_flags);
						/* retry the login again */
						ql_dbg(ql_dbg_disc, vha, 0x2007,
						    "Retrying %d login again loop_id 0x%x.\n",
						    fcport->login_retry,
						    fcport->loop_id);
					} else {
						fcport->login_retry = 0;
					}

					if (fcport->login_retry == 0 &&
					    status != QLA_SUCCESS)
						qla2x00_clear_loop_id(fcport);
				}
			}
		}
		if (test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags))
			break;
	}

	if (relogin_needed)
		set_bit(RELOGIN_NEEDED, &vha->dpc_flags);

	ql_dbg(ql_dbg_disc, vha, 0x400e,
	    "Relogin end.\n");
}

/* Schedule work on any of the dpc-workqueues */
void
qla83xx_schedule_work(scsi_qla_host_t *base_vha, int work_code)
{
	struct qla_hw_data *ha = base_vha->hw;

	switch (work_code) {
	case MBA_IDC_AEN: /* 0x8200 */
		if (ha->dpc_lp_wq)
			queue_work(ha->dpc_lp_wq, &ha->idc_aen);
		break;

	case QLA83XX_NIC_CORE_RESET: /* 0x1 */
		if (!ha->flags.nic_core_reset_hdlr_active) {
			if (ha->dpc_hp_wq)
				queue_work(ha->dpc_hp_wq, &ha->nic_core_reset);
		} else
			ql_dbg(ql_dbg_p3p, base_vha, 0xb05e,
			    "NIC Core reset is already active. Skip "
			    "scheduling it again.\n");
		break;
	case QLA83XX_IDC_STATE_HANDLER: /* 0x2 */
		if (ha->dpc_hp_wq)
			queue_work(ha->dpc_hp_wq, &ha->idc_state_handler);
		break;
	case QLA83XX_NIC_CORE_UNRECOVERABLE: /* 0x3 */
		if (ha->dpc_hp_wq)
			queue_work(ha->dpc_hp_wq, &ha->nic_core_unrecoverable);
		break;
	default:
		ql_log(ql_log_warn, base_vha, 0xb05f,
		    "Unknown work-code=0x%x.\n", work_code);
	}

	return;
}

/* Work: Perform NIC Core Unrecoverable state handling */
void
qla83xx_nic_core_unrecoverable_work(struct work_struct *work)
{
	struct qla_hw_data *ha =
		container_of(work, struct qla_hw_data, nic_core_unrecoverable);
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);
	uint32_t dev_state = 0;

	qla83xx_idc_lock(base_vha, 0);
	qla83xx_rd_reg(base_vha, QLA83XX_IDC_DEV_STATE, &dev_state);
	qla83xx_reset_ownership(base_vha);
	if (ha->flags.nic_core_reset_owner) {
		ha->flags.nic_core_reset_owner = 0;
		qla83xx_wr_reg(base_vha, QLA83XX_IDC_DEV_STATE,
		    QLA8XXX_DEV_FAILED);
		ql_log(ql_log_info, base_vha, 0xb060, "HW State: FAILED.\n");
		qla83xx_schedule_work(base_vha, QLA83XX_IDC_STATE_HANDLER);
	}
	qla83xx_idc_unlock(base_vha, 0);
}

/* Work: Execute IDC state handler */
void
qla83xx_idc_state_handler_work(struct work_struct *work)
{
	struct qla_hw_data *ha =
		container_of(work, struct qla_hw_data, idc_state_handler);
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);
	uint32_t dev_state = 0;

	qla83xx_idc_lock(base_vha, 0);
	qla83xx_rd_reg(base_vha, QLA83XX_IDC_DEV_STATE, &dev_state);
	if (dev_state == QLA8XXX_DEV_FAILED ||
			dev_state == QLA8XXX_DEV_NEED_QUIESCENT)
		qla83xx_idc_state_handler(base_vha);
	qla83xx_idc_unlock(base_vha, 0);
}

static int
qla83xx_check_nic_core_fw_alive(scsi_qla_host_t *base_vha)
{
	int rval = QLA_SUCCESS;
	unsigned long heart_beat_wait = jiffies + (1 * HZ);
	uint32_t heart_beat_counter1, heart_beat_counter2;

	do {
		if (time_after(jiffies, heart_beat_wait)) {
			ql_dbg(ql_dbg_p3p, base_vha, 0xb07c,
			    "Nic Core f/w is not alive.\n");
			rval = QLA_FUNCTION_FAILED;
			break;
		}

		qla83xx_idc_lock(base_vha, 0);
		qla83xx_rd_reg(base_vha, QLA83XX_FW_HEARTBEAT,
		    &heart_beat_counter1);
		qla83xx_idc_unlock(base_vha, 0);
		msleep(100);
		qla83xx_idc_lock(base_vha, 0);
		qla83xx_rd_reg(base_vha, QLA83XX_FW_HEARTBEAT,
		    &heart_beat_counter2);
		qla83xx_idc_unlock(base_vha, 0);
	} while (heart_beat_counter1 == heart_beat_counter2);

	return rval;
}

/* Work: Perform NIC Core Reset handling */
void
qla83xx_nic_core_reset_work(struct work_struct *work)
{
	struct qla_hw_data *ha =
		container_of(work, struct qla_hw_data, nic_core_reset);
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);
	uint32_t dev_state = 0;

	if (IS_QLA2031(ha)) {
		if (qla2xxx_mctp_dump(base_vha) != QLA_SUCCESS)
			ql_log(ql_log_warn, base_vha, 0xb081,
			    "Failed to dump mctp\n");
		return;
	}

	if (!ha->flags.nic_core_reset_hdlr_active) {
		if (qla83xx_check_nic_core_fw_alive(base_vha) == QLA_SUCCESS) {
			qla83xx_idc_lock(base_vha, 0);
			qla83xx_rd_reg(base_vha, QLA83XX_IDC_DEV_STATE,
			    &dev_state);
			qla83xx_idc_unlock(base_vha, 0);
			if (dev_state != QLA8XXX_DEV_NEED_RESET) {
				ql_dbg(ql_dbg_p3p, base_vha, 0xb07a,
				    "Nic Core f/w is alive.\n");
				return;
			}
		}

		ha->flags.nic_core_reset_hdlr_active = 1;
		if (qla83xx_nic_core_reset(base_vha)) {
			/* NIC Core reset failed. */
			ql_dbg(ql_dbg_p3p, base_vha, 0xb061,
			    "NIC Core reset failed.\n");
		}
		ha->flags.nic_core_reset_hdlr_active = 0;
	}
}

/* Work: Handle 8200 IDC aens */
void
qla83xx_service_idc_aen(struct work_struct *work)
{
	struct qla_hw_data *ha =
		container_of(work, struct qla_hw_data, idc_aen);
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);
	uint32_t dev_state, idc_control;

	qla83xx_idc_lock(base_vha, 0);
	qla83xx_rd_reg(base_vha, QLA83XX_IDC_DEV_STATE, &dev_state);
	qla83xx_rd_reg(base_vha, QLA83XX_IDC_CONTROL, &idc_control);
	qla83xx_idc_unlock(base_vha, 0);
	if (dev_state == QLA8XXX_DEV_NEED_RESET) {
		if (idc_control & QLA83XX_IDC_GRACEFUL_RESET) {
			ql_dbg(ql_dbg_p3p, base_vha, 0xb062,
			    "Application requested NIC Core Reset.\n");
			qla83xx_schedule_work(base_vha, QLA83XX_NIC_CORE_RESET);
		} else if (qla83xx_check_nic_core_fw_alive(base_vha) ==
		    QLA_SUCCESS) {
			ql_dbg(ql_dbg_p3p, base_vha, 0xb07b,
			    "Other protocol driver requested NIC Core Reset.\n");
			qla83xx_schedule_work(base_vha, QLA83XX_NIC_CORE_RESET);
		}
	} else if (dev_state == QLA8XXX_DEV_FAILED ||
			dev_state == QLA8XXX_DEV_NEED_QUIESCENT) {
		qla83xx_schedule_work(base_vha, QLA83XX_IDC_STATE_HANDLER);
	}
}

static void
qla83xx_wait_logic(void)
{
	int i;

	/* Yield CPU */
	if (!in_interrupt()) {
		/*
		 * Wait about 200ms before retrying again.
		 * This controls the number of retries for single
		 * lock operation.
		 */
		msleep(100);
		schedule();
	} else {
		for (i = 0; i < 20; i++)
			cpu_relax(); /* This a nop instr on i386 */
	}
}

static int
qla83xx_force_lock_recovery(scsi_qla_host_t *base_vha)
{
	int rval;
	uint32_t data;
	uint32_t idc_lck_rcvry_stage_mask = 0x3;
	uint32_t idc_lck_rcvry_owner_mask = 0x3c;
	struct qla_hw_data *ha = base_vha->hw;
	ql_dbg(ql_dbg_p3p, base_vha, 0xb086,
	    "Trying force recovery of the IDC lock.\n");

	rval = qla83xx_rd_reg(base_vha, QLA83XX_IDC_LOCK_RECOVERY, &data);
	if (rval)
		return rval;

	if ((data & idc_lck_rcvry_stage_mask) > 0) {
		return QLA_SUCCESS;
	} else {
		data = (IDC_LOCK_RECOVERY_STAGE1) | (ha->portnum << 2);
		rval = qla83xx_wr_reg(base_vha, QLA83XX_IDC_LOCK_RECOVERY,
		    data);
		if (rval)
			return rval;

		msleep(200);

		rval = qla83xx_rd_reg(base_vha, QLA83XX_IDC_LOCK_RECOVERY,
		    &data);
		if (rval)
			return rval;

		if (((data & idc_lck_rcvry_owner_mask) >> 2) == ha->portnum) {
			data &= (IDC_LOCK_RECOVERY_STAGE2 |
					~(idc_lck_rcvry_stage_mask));
			rval = qla83xx_wr_reg(base_vha,
			    QLA83XX_IDC_LOCK_RECOVERY, data);
			if (rval)
				return rval;

			/* Forcefully perform IDC UnLock */
			rval = qla83xx_rd_reg(base_vha, QLA83XX_DRIVER_UNLOCK,
			    &data);
			if (rval)
				return rval;
			/* Clear lock-id by setting 0xff */
			rval = qla83xx_wr_reg(base_vha, QLA83XX_DRIVER_LOCKID,
			    0xff);
			if (rval)
				return rval;
			/* Clear lock-recovery by setting 0x0 */
			rval = qla83xx_wr_reg(base_vha,
			    QLA83XX_IDC_LOCK_RECOVERY, 0x0);
			if (rval)
				return rval;
		} else
			return QLA_SUCCESS;
	}

	return rval;
}

static int
qla83xx_idc_lock_recovery(scsi_qla_host_t *base_vha)
{
	int rval = QLA_SUCCESS;
	uint32_t o_drv_lockid, n_drv_lockid;
	unsigned long lock_recovery_timeout;

	lock_recovery_timeout = jiffies + QLA83XX_MAX_LOCK_RECOVERY_WAIT;
retry_lockid:
	rval = qla83xx_rd_reg(base_vha, QLA83XX_DRIVER_LOCKID, &o_drv_lockid);
	if (rval)
		goto exit;

	/* MAX wait time before forcing IDC Lock recovery = 2 secs */
	if (time_after_eq(jiffies, lock_recovery_timeout)) {
		if (qla83xx_force_lock_recovery(base_vha) == QLA_SUCCESS)
			return QLA_SUCCESS;
		else
			return QLA_FUNCTION_FAILED;
	}

	rval = qla83xx_rd_reg(base_vha, QLA83XX_DRIVER_LOCKID, &n_drv_lockid);
	if (rval)
		goto exit;

	if (o_drv_lockid == n_drv_lockid) {
		qla83xx_wait_logic();
		goto retry_lockid;
	} else
		return QLA_SUCCESS;

exit:
	return rval;
}

void
qla83xx_idc_lock(scsi_qla_host_t *base_vha, uint16_t requester_id)
{
	uint16_t options = (requester_id << 15) | BIT_6;
	uint32_t data;
	uint32_t lock_owner;
	struct qla_hw_data *ha = base_vha->hw;

	/* IDC-lock implementation using driver-lock/lock-id remote registers */
retry_lock:
	if (qla83xx_rd_reg(base_vha, QLA83XX_DRIVER_LOCK, &data)
	    == QLA_SUCCESS) {
		if (data) {
			/* Setting lock-id to our function-number */
			qla83xx_wr_reg(base_vha, QLA83XX_DRIVER_LOCKID,
			    ha->portnum);
		} else {
			qla83xx_rd_reg(base_vha, QLA83XX_DRIVER_LOCKID,
			    &lock_owner);
			ql_dbg(ql_dbg_p3p, base_vha, 0xb063,
			    "Failed to acquire IDC lock, acquired by %d, "
			    "retrying...\n", lock_owner);

			/* Retry/Perform IDC-Lock recovery */
			if (qla83xx_idc_lock_recovery(base_vha)
			    == QLA_SUCCESS) {
				qla83xx_wait_logic();
				goto retry_lock;
			} else
				ql_log(ql_log_warn, base_vha, 0xb075,
				    "IDC Lock recovery FAILED.\n");
		}

	}

	return;

	/* XXX: IDC-lock implementation using access-control mbx */
retry_lock2:
	if (qla83xx_access_control(base_vha, options, 0, 0, NULL)) {
		ql_dbg(ql_dbg_p3p, base_vha, 0xb072,
		    "Failed to acquire IDC lock. retrying...\n");
		/* Retry/Perform IDC-Lock recovery */
		if (qla83xx_idc_lock_recovery(base_vha) == QLA_SUCCESS) {
			qla83xx_wait_logic();
			goto retry_lock2;
		} else
			ql_log(ql_log_warn, base_vha, 0xb076,
			    "IDC Lock recovery FAILED.\n");
	}

	return;
}

void
qla83xx_idc_unlock(scsi_qla_host_t *base_vha, uint16_t requester_id)
{
#if 0
	uint16_t options = (requester_id << 15) | BIT_7;
#endif
	uint16_t retry;
	uint32_t data;
	struct qla_hw_data *ha = base_vha->hw;

	/* IDC-unlock implementation using driver-unlock/lock-id
	 * remote registers
	 */
	retry = 0;
retry_unlock:
	if (qla83xx_rd_reg(base_vha, QLA83XX_DRIVER_LOCKID, &data)
	    == QLA_SUCCESS) {
		if (data == ha->portnum) {
			qla83xx_rd_reg(base_vha, QLA83XX_DRIVER_UNLOCK, &data);
			/* Clearing lock-id by setting 0xff */
			qla83xx_wr_reg(base_vha, QLA83XX_DRIVER_LOCKID, 0xff);
		} else if (retry < 10) {
			/* SV: XXX: IDC unlock retrying needed here? */

			/* Retry for IDC-unlock */
			qla83xx_wait_logic();
			retry++;
			ql_dbg(ql_dbg_p3p, base_vha, 0xb064,
			    "Failed to release IDC lock, retrying=%d\n", retry);
			goto retry_unlock;
		}
	} else if (retry < 10) {
		/* Retry for IDC-unlock */
		qla83xx_wait_logic();
		retry++;
		ql_dbg(ql_dbg_p3p, base_vha, 0xb065,
		    "Failed to read drv-lockid, retrying=%d\n", retry);
		goto retry_unlock;
	}

	return;

#if 0
	/* XXX: IDC-unlock implementation using access-control mbx */
	retry = 0;
retry_unlock2:
	if (qla83xx_access_control(base_vha, options, 0, 0, NULL)) {
		if (retry < 10) {
			/* Retry for IDC-unlock */
			qla83xx_wait_logic();
			retry++;
			ql_dbg(ql_dbg_p3p, base_vha, 0xb066,
			    "Failed to release IDC lock, retrying=%d\n", retry);
			goto retry_unlock2;
		}
	}

	return;
#endif
}

int
__qla83xx_set_drv_presence(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;
	uint32_t drv_presence;

	rval = qla83xx_rd_reg(vha, QLA83XX_IDC_DRV_PRESENCE, &drv_presence);
	if (rval == QLA_SUCCESS) {
		drv_presence |= (1 << ha->portnum);
		rval = qla83xx_wr_reg(vha, QLA83XX_IDC_DRV_PRESENCE,
		    drv_presence);
	}

	return rval;
}

int
qla83xx_set_drv_presence(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;

	qla83xx_idc_lock(vha, 0);
	rval = __qla83xx_set_drv_presence(vha);
	qla83xx_idc_unlock(vha, 0);

	return rval;
}

int
__qla83xx_clear_drv_presence(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;
	uint32_t drv_presence;

	rval = qla83xx_rd_reg(vha, QLA83XX_IDC_DRV_PRESENCE, &drv_presence);
	if (rval == QLA_SUCCESS) {
		drv_presence &= ~(1 << ha->portnum);
		rval = qla83xx_wr_reg(vha, QLA83XX_IDC_DRV_PRESENCE,
		    drv_presence);
	}

	return rval;
}

int
qla83xx_clear_drv_presence(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;

	qla83xx_idc_lock(vha, 0);
	rval = __qla83xx_clear_drv_presence(vha);
	qla83xx_idc_unlock(vha, 0);

	return rval;
}

static void
qla83xx_need_reset_handler(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t drv_ack, drv_presence;
	unsigned long ack_timeout;

	/* Wait for IDC ACK from all functions (DRV-ACK == DRV-PRESENCE) */
	ack_timeout = jiffies + (ha->fcoe_reset_timeout * HZ);
	while (1) {
		qla83xx_rd_reg(vha, QLA83XX_IDC_DRIVER_ACK, &drv_ack);
		qla83xx_rd_reg(vha, QLA83XX_IDC_DRV_PRESENCE, &drv_presence);
		if ((drv_ack & drv_presence) == drv_presence)
			break;

		if (time_after_eq(jiffies, ack_timeout)) {
			ql_log(ql_log_warn, vha, 0xb067,
			    "RESET ACK TIMEOUT! drv_presence=0x%x "
			    "drv_ack=0x%x\n", drv_presence, drv_ack);
			/*
			 * The function(s) which did not ack in time are forced
			 * to withdraw any further participation in the IDC
			 * reset.
			 */
			if (drv_ack != drv_presence)
				qla83xx_wr_reg(vha, QLA83XX_IDC_DRV_PRESENCE,
				    drv_ack);
			break;
		}

		qla83xx_idc_unlock(vha, 0);
		msleep(1000);
		qla83xx_idc_lock(vha, 0);
	}

	qla83xx_wr_reg(vha, QLA83XX_IDC_DEV_STATE, QLA8XXX_DEV_COLD);
	ql_log(ql_log_info, vha, 0xb068, "HW State: COLD/RE-INIT.\n");
}

static int
qla83xx_device_bootstrap(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;
	uint32_t idc_control;

	qla83xx_wr_reg(vha, QLA83XX_IDC_DEV_STATE, QLA8XXX_DEV_INITIALIZING);
	ql_log(ql_log_info, vha, 0xb069, "HW State: INITIALIZING.\n");

	/* Clearing IDC-Control Graceful-Reset Bit before resetting f/w */
	__qla83xx_get_idc_control(vha, &idc_control);
	idc_control &= ~QLA83XX_IDC_GRACEFUL_RESET;
	__qla83xx_set_idc_control(vha, 0);

	qla83xx_idc_unlock(vha, 0);
	rval = qla83xx_restart_nic_firmware(vha);
	qla83xx_idc_lock(vha, 0);

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0xb06a,
		    "Failed to restart NIC f/w.\n");
		qla83xx_wr_reg(vha, QLA83XX_IDC_DEV_STATE, QLA8XXX_DEV_FAILED);
		ql_log(ql_log_info, vha, 0xb06b, "HW State: FAILED.\n");
	} else {
		ql_dbg(ql_dbg_p3p, vha, 0xb06c,
		    "Success in restarting nic f/w.\n");
		qla83xx_wr_reg(vha, QLA83XX_IDC_DEV_STATE, QLA8XXX_DEV_READY);
		ql_log(ql_log_info, vha, 0xb06d, "HW State: READY.\n");
	}

	return rval;
}

/* Assumes idc_lock always held on entry */
int
qla83xx_idc_state_handler(scsi_qla_host_t *base_vha)
{
	struct qla_hw_data *ha = base_vha->hw;
	int rval = QLA_SUCCESS;
	unsigned long dev_init_timeout;
	uint32_t dev_state;

	/* Wait for MAX-INIT-TIMEOUT for the device to go ready */
	dev_init_timeout = jiffies + (ha->fcoe_dev_init_timeout * HZ);

	while (1) {

		if (time_after_eq(jiffies, dev_init_timeout)) {
			ql_log(ql_log_warn, base_vha, 0xb06e,
			    "Initialization TIMEOUT!\n");
			/* Init timeout. Disable further NIC Core
			 * communication.
			 */
			qla83xx_wr_reg(base_vha, QLA83XX_IDC_DEV_STATE,
				QLA8XXX_DEV_FAILED);
			ql_log(ql_log_info, base_vha, 0xb06f,
			    "HW State: FAILED.\n");
		}

		qla83xx_rd_reg(base_vha, QLA83XX_IDC_DEV_STATE, &dev_state);
		switch (dev_state) {
		case QLA8XXX_DEV_READY:
			if (ha->flags.nic_core_reset_owner)
				qla83xx_idc_audit(base_vha,
				    IDC_AUDIT_COMPLETION);
			ha->flags.nic_core_reset_owner = 0;
			ql_dbg(ql_dbg_p3p, base_vha, 0xb070,
			    "Reset_owner reset by 0x%x.\n",
			    ha->portnum);
			goto exit;
		case QLA8XXX_DEV_COLD:
			if (ha->flags.nic_core_reset_owner)
				rval = qla83xx_device_bootstrap(base_vha);
			else {
			/* Wait for AEN to change device-state */
				qla83xx_idc_unlock(base_vha, 0);
				msleep(1000);
				qla83xx_idc_lock(base_vha, 0);
			}
			break;
		case QLA8XXX_DEV_INITIALIZING:
			/* Wait for AEN to change device-state */
			qla83xx_idc_unlock(base_vha, 0);
			msleep(1000);
			qla83xx_idc_lock(base_vha, 0);
			break;
		case QLA8XXX_DEV_NEED_RESET:
			if (!ql2xdontresethba && ha->flags.nic_core_reset_owner)
				qla83xx_need_reset_handler(base_vha);
			else {
				/* Wait for AEN to change device-state */
				qla83xx_idc_unlock(base_vha, 0);
				msleep(1000);
				qla83xx_idc_lock(base_vha, 0);
			}
			/* reset timeout value after need reset handler */
			dev_init_timeout = jiffies +
			    (ha->fcoe_dev_init_timeout * HZ);
			break;
		case QLA8XXX_DEV_NEED_QUIESCENT:
			/* XXX: DEBUG for now */
			qla83xx_idc_unlock(base_vha, 0);
			msleep(1000);
			qla83xx_idc_lock(base_vha, 0);
			break;
		case QLA8XXX_DEV_QUIESCENT:
			/* XXX: DEBUG for now */
			if (ha->flags.quiesce_owner)
				goto exit;

			qla83xx_idc_unlock(base_vha, 0);
			msleep(1000);
			qla83xx_idc_lock(base_vha, 0);
			dev_init_timeout = jiffies +
			    (ha->fcoe_dev_init_timeout * HZ);
			break;
		case QLA8XXX_DEV_FAILED:
			if (ha->flags.nic_core_reset_owner)
				qla83xx_idc_audit(base_vha,
				    IDC_AUDIT_COMPLETION);
			ha->flags.nic_core_reset_owner = 0;
			__qla83xx_clear_drv_presence(base_vha);
			qla83xx_idc_unlock(base_vha, 0);
			qla8xxx_dev_failed_handler(base_vha);
			rval = QLA_FUNCTION_FAILED;
			qla83xx_idc_lock(base_vha, 0);
			goto exit;
		case QLA8XXX_BAD_VALUE:
			qla83xx_idc_unlock(base_vha, 0);
			msleep(1000);
			qla83xx_idc_lock(base_vha, 0);
			break;
		default:
			ql_log(ql_log_warn, base_vha, 0xb071,
			    "Unknown Device State: %x.\n", dev_state);
			qla83xx_idc_unlock(base_vha, 0);
			qla8xxx_dev_failed_handler(base_vha);
			rval = QLA_FUNCTION_FAILED;
			qla83xx_idc_lock(base_vha, 0);
			goto exit;
		}
	}

exit:
	return rval;
}

void
qla2x00_disable_board_on_pci_error(struct work_struct *work)
{
	struct qla_hw_data *ha = container_of(work, struct qla_hw_data,
	    board_disable);
	struct pci_dev *pdev = ha->pdev;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	/*
	 * if UNLOAD flag is already set, then continue unload,
	 * where it was set first.
	 */
	if (test_bit(UNLOADING, &base_vha->dpc_flags))
		return;

	ql_log(ql_log_warn, base_vha, 0x015b,
	    "Disabling adapter.\n");

	if (!atomic_read(&pdev->enable_cnt)) {
		ql_log(ql_log_info, base_vha, 0xfffc,
		    "PCI device disabled, no action req for PCI error=%lx\n",
		    base_vha->pci_flags);
		return;
	}

	qla2x00_wait_for_sess_deletion(base_vha);

	set_bit(UNLOADING, &base_vha->dpc_flags);

	qla2x00_delete_all_vps(ha, base_vha);

	qla2x00_abort_all_cmds(base_vha, DID_NO_CONNECT << 16);

	qla2x00_dfs_remove(base_vha);

	qla84xx_put_chip(base_vha);

	if (base_vha->timer_active)
		qla2x00_stop_timer(base_vha);

	base_vha->flags.online = 0;

	qla2x00_destroy_deferred_work(ha);

	/*
	 * Do not try to stop beacon blink as it will issue a mailbox
	 * command.
	 */
	qla2x00_free_sysfs_attr(base_vha, false);

	fc_remove_host(base_vha->host);

	scsi_remove_host(base_vha->host);

	base_vha->flags.init_done = 0;
	qla25xx_delete_queues(base_vha);
	qla2x00_free_fcports(base_vha);
	qla2x00_free_irqs(base_vha);
	qla2x00_mem_free(ha);
	qla82xx_md_free(base_vha);
	qla2x00_free_queues(ha);

	qla2x00_unmap_iobases(ha);

	pci_release_selected_regions(ha->pdev, ha->bars);
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);

	/*
	 * Let qla2x00_remove_one cleanup qla_hw_data on device removal.
	 */
}

/**************************************************************************
* qla2x00_do_dpc
*   This kernel thread is a task that is schedule by the interrupt handler
*   to perform the background processing for interrupts.
*
* Notes:
* This task always run in the context of a kernel thread.  It
* is kick-off by the driver's detect code and starts up
* up one per adapter. It immediately goes to sleep and waits for
* some fibre event.  When either the interrupt handler or
* the timer routine detects a event it will one of the task
* bits then wake us up.
**************************************************************************/
static int
qla2x00_do_dpc(void *data)
{
	scsi_qla_host_t *base_vha;
	struct qla_hw_data *ha;
	uint32_t online;
	struct qla_qpair *qpair;

	ha = (struct qla_hw_data *)data;
	base_vha = pci_get_drvdata(ha->pdev);

	set_user_nice(current, MIN_NICE);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		ql_dbg(ql_dbg_dpc, base_vha, 0x4000,
		    "DPC handler sleeping.\n");

		schedule();

		if (!base_vha->flags.init_done || ha->flags.mbox_busy)
			goto end_loop;

		if (ha->flags.eeh_busy) {
			ql_dbg(ql_dbg_dpc, base_vha, 0x4003,
			    "eeh_busy=%d.\n", ha->flags.eeh_busy);
			goto end_loop;
		}

		ha->dpc_active = 1;

		ql_dbg(ql_dbg_dpc + ql_dbg_verbose, base_vha, 0x4001,
		    "DPC handler waking up, dpc_flags=0x%lx.\n",
		    base_vha->dpc_flags);

		if (test_bit(UNLOADING, &base_vha->dpc_flags))
			break;

		if (IS_P3P_TYPE(ha)) {
			if (IS_QLA8044(ha)) {
				if (test_and_clear_bit(ISP_UNRECOVERABLE,
					&base_vha->dpc_flags)) {
					qla8044_idc_lock(ha);
					qla8044_wr_direct(base_vha,
						QLA8044_CRB_DEV_STATE_INDEX,
						QLA8XXX_DEV_FAILED);
					qla8044_idc_unlock(ha);
					ql_log(ql_log_info, base_vha, 0x4004,
						"HW State: FAILED.\n");
					qla8044_device_state_handler(base_vha);
					continue;
				}

			} else {
				if (test_and_clear_bit(ISP_UNRECOVERABLE,
					&base_vha->dpc_flags)) {
					qla82xx_idc_lock(ha);
					qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
						QLA8XXX_DEV_FAILED);
					qla82xx_idc_unlock(ha);
					ql_log(ql_log_info, base_vha, 0x0151,
						"HW State: FAILED.\n");
					qla82xx_device_state_handler(base_vha);
					continue;
				}
			}

			if (test_and_clear_bit(FCOE_CTX_RESET_NEEDED,
				&base_vha->dpc_flags)) {

				ql_dbg(ql_dbg_dpc, base_vha, 0x4005,
				    "FCoE context reset scheduled.\n");
				if (!(test_and_set_bit(ABORT_ISP_ACTIVE,
					&base_vha->dpc_flags))) {
					if (qla82xx_fcoe_ctx_reset(base_vha)) {
						/* FCoE-ctx reset failed.
						 * Escalate to chip-reset
						 */
						set_bit(ISP_ABORT_NEEDED,
							&base_vha->dpc_flags);
					}
					clear_bit(ABORT_ISP_ACTIVE,
						&base_vha->dpc_flags);
				}

				ql_dbg(ql_dbg_dpc, base_vha, 0x4006,
				    "FCoE context reset end.\n");
			}
		} else if (IS_QLAFX00(ha)) {
			if (test_and_clear_bit(ISP_UNRECOVERABLE,
				&base_vha->dpc_flags)) {
				ql_dbg(ql_dbg_dpc, base_vha, 0x4020,
				    "Firmware Reset Recovery\n");
				if (qlafx00_reset_initialize(base_vha)) {
					/* Failed. Abort isp later. */
					if (!test_bit(UNLOADING,
					    &base_vha->dpc_flags)) {
						set_bit(ISP_UNRECOVERABLE,
						    &base_vha->dpc_flags);
						ql_dbg(ql_dbg_dpc, base_vha,
						    0x4021,
						    "Reset Recovery Failed\n");
					}
				}
			}

			if (test_and_clear_bit(FX00_TARGET_SCAN,
				&base_vha->dpc_flags)) {
				ql_dbg(ql_dbg_dpc, base_vha, 0x4022,
				    "ISPFx00 Target Scan scheduled\n");
				if (qlafx00_rescan_isp(base_vha)) {
					if (!test_bit(UNLOADING,
					    &base_vha->dpc_flags))
						set_bit(ISP_UNRECOVERABLE,
						    &base_vha->dpc_flags);
					ql_dbg(ql_dbg_dpc, base_vha, 0x401e,
					    "ISPFx00 Target Scan Failed\n");
				}
				ql_dbg(ql_dbg_dpc, base_vha, 0x401f,
				    "ISPFx00 Target Scan End\n");
			}
			if (test_and_clear_bit(FX00_HOST_INFO_RESEND,
				&base_vha->dpc_flags)) {
				ql_dbg(ql_dbg_dpc, base_vha, 0x4023,
				    "ISPFx00 Host Info resend scheduled\n");
				qlafx00_fx_disc(base_vha,
				    &base_vha->hw->mr.fcport,
				    FXDISC_REG_HOST_INFO);
			}
		}

		if (test_and_clear_bit(DETECT_SFP_CHANGE,
			&base_vha->dpc_flags) &&
		    !test_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags)) {
			qla24xx_detect_sfp(base_vha);

			if (ha->flags.detected_lr_sfp !=
			    ha->flags.using_lr_setting)
				set_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags);
		}

		if (test_and_clear_bit
		    (ISP_ABORT_NEEDED, &base_vha->dpc_flags) &&
		    !test_bit(UNLOADING, &base_vha->dpc_flags)) {
			bool do_reset = true;

			switch (ql2x_ini_mode) {
			case QLA2XXX_INI_MODE_ENABLED:
				break;
			case QLA2XXX_INI_MODE_DISABLED:
				if (!qla_tgt_mode_enabled(base_vha))
					do_reset = false;
				break;
			case QLA2XXX_INI_MODE_DUAL:
				if (!qla_dual_mode_enabled(base_vha))
					do_reset = false;
				break;
			default:
				break;
			}

			if (do_reset && !(test_and_set_bit(ABORT_ISP_ACTIVE,
			    &base_vha->dpc_flags))) {
				ql_dbg(ql_dbg_dpc, base_vha, 0x4007,
				    "ISP abort scheduled.\n");
				if (ha->isp_ops->abort_isp(base_vha)) {
					/* failed. retry later */
					set_bit(ISP_ABORT_NEEDED,
					    &base_vha->dpc_flags);
				}
				clear_bit(ABORT_ISP_ACTIVE,
						&base_vha->dpc_flags);
				ql_dbg(ql_dbg_dpc, base_vha, 0x4008,
				    "ISP abort end.\n");
			}
		}

		if (test_and_clear_bit(FCPORT_UPDATE_NEEDED,
		    &base_vha->dpc_flags)) {
			qla2x00_update_fcports(base_vha);
		}

		if (IS_QLAFX00(ha))
			goto loop_resync_check;

		if (test_bit(ISP_QUIESCE_NEEDED, &base_vha->dpc_flags)) {
			ql_dbg(ql_dbg_dpc, base_vha, 0x4009,
			    "Quiescence mode scheduled.\n");
			if (IS_P3P_TYPE(ha)) {
				if (IS_QLA82XX(ha))
					qla82xx_device_state_handler(base_vha);
				if (IS_QLA8044(ha))
					qla8044_device_state_handler(base_vha);
				clear_bit(ISP_QUIESCE_NEEDED,
				    &base_vha->dpc_flags);
				if (!ha->flags.quiesce_owner) {
					qla2x00_perform_loop_resync(base_vha);
					if (IS_QLA82XX(ha)) {
						qla82xx_idc_lock(ha);
						qla82xx_clear_qsnt_ready(
						    base_vha);
						qla82xx_idc_unlock(ha);
					} else if (IS_QLA8044(ha)) {
						qla8044_idc_lock(ha);
						qla8044_clear_qsnt_ready(
						    base_vha);
						qla8044_idc_unlock(ha);
					}
				}
			} else {
				clear_bit(ISP_QUIESCE_NEEDED,
				    &base_vha->dpc_flags);
				qla2x00_quiesce_io(base_vha);
			}
			ql_dbg(ql_dbg_dpc, base_vha, 0x400a,
			    "Quiescence mode end.\n");
		}

		if (test_and_clear_bit(RESET_MARKER_NEEDED,
				&base_vha->dpc_flags) &&
		    (!(test_and_set_bit(RESET_ACTIVE, &base_vha->dpc_flags)))) {

			ql_dbg(ql_dbg_dpc, base_vha, 0x400b,
			    "Reset marker scheduled.\n");
			qla2x00_rst_aen(base_vha);
			clear_bit(RESET_ACTIVE, &base_vha->dpc_flags);
			ql_dbg(ql_dbg_dpc, base_vha, 0x400c,
			    "Reset marker end.\n");
		}

		/* Retry each device up to login retry count */
		if (test_bit(RELOGIN_NEEDED, &base_vha->dpc_flags) &&
		    !test_bit(LOOP_RESYNC_NEEDED, &base_vha->dpc_flags) &&
		    atomic_read(&base_vha->loop_state) != LOOP_DOWN) {

			if (!base_vha->relogin_jif ||
			    time_after_eq(jiffies, base_vha->relogin_jif)) {
				base_vha->relogin_jif = jiffies + HZ;
				clear_bit(RELOGIN_NEEDED, &base_vha->dpc_flags);

				ql_dbg(ql_dbg_disc, base_vha, 0x400d,
				    "Relogin scheduled.\n");
				qla24xx_post_relogin_work(base_vha);
			}
		}
loop_resync_check:
		if (test_and_clear_bit(LOOP_RESYNC_NEEDED,
		    &base_vha->dpc_flags)) {

			ql_dbg(ql_dbg_dpc, base_vha, 0x400f,
			    "Loop resync scheduled.\n");

			if (!(test_and_set_bit(LOOP_RESYNC_ACTIVE,
			    &base_vha->dpc_flags))) {

				qla2x00_loop_resync(base_vha);

				clear_bit(LOOP_RESYNC_ACTIVE,
						&base_vha->dpc_flags);
			}

			ql_dbg(ql_dbg_dpc, base_vha, 0x4010,
			    "Loop resync end.\n");
		}

		if (IS_QLAFX00(ha))
			goto intr_on_check;

		if (test_bit(NPIV_CONFIG_NEEDED, &base_vha->dpc_flags) &&
		    atomic_read(&base_vha->loop_state) == LOOP_READY) {
			clear_bit(NPIV_CONFIG_NEEDED, &base_vha->dpc_flags);
			qla2xxx_flash_npiv_conf(base_vha);
		}

intr_on_check:
		if (!ha->interrupts_on)
			ha->isp_ops->enable_intrs(ha);

		if (test_and_clear_bit(BEACON_BLINK_NEEDED,
					&base_vha->dpc_flags)) {
			if (ha->beacon_blink_led == 1)
				ha->isp_ops->beacon_blink(base_vha);
		}

		/* qpair online check */
		if (test_and_clear_bit(QPAIR_ONLINE_CHECK_NEEDED,
		    &base_vha->dpc_flags)) {
			if (ha->flags.eeh_busy ||
			    ha->flags.pci_channel_io_perm_failure)
				online = 0;
			else
				online = 1;

			mutex_lock(&ha->mq_lock);
			list_for_each_entry(qpair, &base_vha->qp_list,
			    qp_list_elem)
			qpair->online = online;
			mutex_unlock(&ha->mq_lock);
		}

		if (test_and_clear_bit(SET_ZIO_THRESHOLD_NEEDED, &base_vha->dpc_flags)) {
			ql_log(ql_log_info, base_vha, 0xffffff,
				"nvme: SET ZIO Activity exchange threshold to %d.\n",
						ha->nvme_last_rptd_aen);
			if (qla27xx_set_zio_threshold(base_vha, ha->nvme_last_rptd_aen)) {
				ql_log(ql_log_info, base_vha, 0xffffff,
					"nvme: Unable to SET ZIO Activity exchange threshold to %d.\n",
						ha->nvme_last_rptd_aen);
			}
		}

		if (!IS_QLAFX00(ha))
			qla2x00_do_dpc_all_vps(base_vha);

		if (test_and_clear_bit(N2N_LINK_RESET,
			&base_vha->dpc_flags)) {
			qla2x00_lip_reset(base_vha);
		}

		ha->dpc_active = 0;
end_loop:
		set_current_state(TASK_INTERRUPTIBLE);
	} /* End of while(1) */
	__set_current_state(TASK_RUNNING);

	ql_dbg(ql_dbg_dpc, base_vha, 0x4011,
	    "DPC handler exiting.\n");

	/*
	 * Make sure that nobody tries to wake us up again.
	 */
	ha->dpc_active = 0;

	/* Cleanup any residual CTX SRBs. */
	qla2x00_abort_all_cmds(base_vha, DID_NO_CONNECT << 16);

	return 0;
}

void
qla2xxx_wake_dpc(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct task_struct *t = ha->dpc_thread;

	if (!test_bit(UNLOADING, &vha->dpc_flags) && t)
		wake_up_process(t);
}

/*
*  qla2x00_rst_aen
*      Processes asynchronous reset.
*
* Input:
*      ha  = adapter block pointer.
*/
static void
qla2x00_rst_aen(scsi_qla_host_t *vha)
{
	if (vha->flags.online && !vha->flags.reset_active &&
	    !atomic_read(&vha->loop_down_timer) &&
	    !(test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags))) {
		do {
			clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);

			/*
			 * Issue marker command only when we are going to start
			 * the I/O.
			 */
			vha->marker_needed = 1;
		} while (!atomic_read(&vha->loop_down_timer) &&
		    (test_bit(RESET_MARKER_NEEDED, &vha->dpc_flags)));
	}
}

/**************************************************************************
*   qla2x00_timer
*
* Description:
*   One second timer
*
* Context: Interrupt
***************************************************************************/
void
qla2x00_timer(struct timer_list *t)
{
	scsi_qla_host_t *vha = from_timer(vha, t, timer);
	unsigned long	cpu_flags = 0;
	int		start_dpc = 0;
	int		index;
	srb_t		*sp;
	uint16_t        w;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;

	if (ha->flags.eeh_busy) {
		ql_dbg(ql_dbg_timer, vha, 0x6000,
		    "EEH = %d, restarting timer.\n",
		    ha->flags.eeh_busy);
		qla2x00_restart_timer(vha, WATCH_INTERVAL);
		return;
	}

	/*
	 * Hardware read to raise pending EEH errors during mailbox waits. If
	 * the read returns -1 then disable the board.
	 */
	if (!pci_channel_offline(ha->pdev)) {
		pci_read_config_word(ha->pdev, PCI_VENDOR_ID, &w);
		qla2x00_check_reg16_for_disconnect(vha, w);
	}

	/* Make sure qla82xx_watchdog is run only for physical port */
	if (!vha->vp_idx && IS_P3P_TYPE(ha)) {
		if (test_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags))
			start_dpc++;
		if (IS_QLA82XX(ha))
			qla82xx_watchdog(vha);
		else if (IS_QLA8044(ha))
			qla8044_watchdog(vha);
	}

	if (!vha->vp_idx && IS_QLAFX00(ha))
		qlafx00_timer_routine(vha);

	/* Loop down handler. */
	if (atomic_read(&vha->loop_down_timer) > 0 &&
	    !(test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags)) &&
	    !(test_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags))
		&& vha->flags.online) {

		if (atomic_read(&vha->loop_down_timer) ==
		    vha->loop_down_abort_time) {

			ql_log(ql_log_info, vha, 0x6008,
			    "Loop down - aborting the queues before time expires.\n");

			if (!IS_QLA2100(ha) && vha->link_down_timeout)
				atomic_set(&vha->loop_state, LOOP_DEAD);

			/*
			 * Schedule an ISP abort to return any FCP2-device
			 * commands.
			 */
			/* NPIV - scan physical port only */
			if (!vha->vp_idx) {
				spin_lock_irqsave(&ha->hardware_lock,
				    cpu_flags);
				req = ha->req_q_map[0];
				for (index = 1;
				    index < req->num_outstanding_cmds;
				    index++) {
					fc_port_t *sfcp;

					sp = req->outstanding_cmds[index];
					if (!sp)
						continue;
					if (sp->cmd_type != TYPE_SRB)
						continue;
					if (sp->type != SRB_SCSI_CMD)
						continue;
					sfcp = sp->fcport;
					if (!(sfcp->flags & FCF_FCP2_DEVICE))
						continue;

					if (IS_QLA82XX(ha))
						set_bit(FCOE_CTX_RESET_NEEDED,
							&vha->dpc_flags);
					else
						set_bit(ISP_ABORT_NEEDED,
							&vha->dpc_flags);
					break;
				}
				spin_unlock_irqrestore(&ha->hardware_lock,
								cpu_flags);
			}
			start_dpc++;
		}

		/* if the loop has been down for 4 minutes, reinit adapter */
		if (atomic_dec_and_test(&vha->loop_down_timer) != 0) {
			if (!(vha->device_flags & DFLG_NO_CABLE)) {
				ql_log(ql_log_warn, vha, 0x6009,
				    "Loop down - aborting ISP.\n");

				if (IS_QLA82XX(ha))
					set_bit(FCOE_CTX_RESET_NEEDED,
						&vha->dpc_flags);
				else
					set_bit(ISP_ABORT_NEEDED,
						&vha->dpc_flags);
			}
		}
		ql_dbg(ql_dbg_timer, vha, 0x600a,
		    "Loop down - seconds remaining %d.\n",
		    atomic_read(&vha->loop_down_timer));
	}
	/* Check if beacon LED needs to be blinked for physical host only */
	if (!vha->vp_idx && (ha->beacon_blink_led == 1)) {
		/* There is no beacon_blink function for ISP82xx */
		if (!IS_P3P_TYPE(ha)) {
			set_bit(BEACON_BLINK_NEEDED, &vha->dpc_flags);
			start_dpc++;
		}
	}

	/* Process any deferred work. */
	if (!list_empty(&vha->work_list)) {
		unsigned long flags;
		bool q = false;

		spin_lock_irqsave(&vha->work_lock, flags);
		if (!test_and_set_bit(IOCB_WORK_ACTIVE, &vha->dpc_flags))
			q = true;
		spin_unlock_irqrestore(&vha->work_lock, flags);
		if (q)
			queue_work(vha->hw->wq, &vha->iocb_work);
	}

	/*
	 * FC-NVME
	 * see if the active AEN count has changed from what was last reported.
	 */
	if (!vha->vp_idx &&
		atomic_read(&ha->nvme_active_aen_cnt) != ha->nvme_last_rptd_aen &&
		ha->zio_mode == QLA_ZIO_MODE_6) {
		ql_log(ql_log_info, vha, 0x3002,
			"nvme: Sched: Set ZIO exchange threshold to %d.\n",
			ha->nvme_last_rptd_aen);
		ha->nvme_last_rptd_aen = atomic_read(&ha->nvme_active_aen_cnt);
		set_bit(SET_ZIO_THRESHOLD_NEEDED, &vha->dpc_flags);
		start_dpc++;
	}

	/* Schedule the DPC routine if needed */
	if ((test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
	    test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags) ||
	    test_bit(FCPORT_UPDATE_NEEDED, &vha->dpc_flags) ||
	    start_dpc ||
	    test_bit(RESET_MARKER_NEEDED, &vha->dpc_flags) ||
	    test_bit(BEACON_BLINK_NEEDED, &vha->dpc_flags) ||
	    test_bit(ISP_UNRECOVERABLE, &vha->dpc_flags) ||
	    test_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags) ||
	    test_bit(VP_DPC_NEEDED, &vha->dpc_flags) ||
	    test_bit(RELOGIN_NEEDED, &vha->dpc_flags))) {
		ql_dbg(ql_dbg_timer, vha, 0x600b,
		    "isp_abort_needed=%d loop_resync_needed=%d "
		    "fcport_update_needed=%d start_dpc=%d "
		    "reset_marker_needed=%d",
		    test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags),
		    test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags),
		    test_bit(FCPORT_UPDATE_NEEDED, &vha->dpc_flags),
		    start_dpc,
		    test_bit(RESET_MARKER_NEEDED, &vha->dpc_flags));
		ql_dbg(ql_dbg_timer, vha, 0x600c,
		    "beacon_blink_needed=%d isp_unrecoverable=%d "
		    "fcoe_ctx_reset_needed=%d vp_dpc_needed=%d "
		    "relogin_needed=%d.\n",
		    test_bit(BEACON_BLINK_NEEDED, &vha->dpc_flags),
		    test_bit(ISP_UNRECOVERABLE, &vha->dpc_flags),
		    test_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags),
		    test_bit(VP_DPC_NEEDED, &vha->dpc_flags),
		    test_bit(RELOGIN_NEEDED, &vha->dpc_flags));
		qla2xxx_wake_dpc(vha);
	}

	qla2x00_restart_timer(vha, WATCH_INTERVAL);
}

/* Firmware interface routines. */

#define FW_BLOBS	11
#define FW_ISP21XX	0
#define FW_ISP22XX	1
#define FW_ISP2300	2
#define FW_ISP2322	3
#define FW_ISP24XX	4
#define FW_ISP25XX	5
#define FW_ISP81XX	6
#define FW_ISP82XX	7
#define FW_ISP2031	8
#define FW_ISP8031	9
#define FW_ISP27XX	10

#define FW_FILE_ISP21XX	"ql2100_fw.bin"
#define FW_FILE_ISP22XX	"ql2200_fw.bin"
#define FW_FILE_ISP2300	"ql2300_fw.bin"
#define FW_FILE_ISP2322	"ql2322_fw.bin"
#define FW_FILE_ISP24XX	"ql2400_fw.bin"
#define FW_FILE_ISP25XX	"ql2500_fw.bin"
#define FW_FILE_ISP81XX	"ql8100_fw.bin"
#define FW_FILE_ISP82XX	"ql8200_fw.bin"
#define FW_FILE_ISP2031	"ql2600_fw.bin"
#define FW_FILE_ISP8031	"ql8300_fw.bin"
#define FW_FILE_ISP27XX	"ql2700_fw.bin"


static DEFINE_MUTEX(qla_fw_lock);

static struct fw_blob qla_fw_blobs[FW_BLOBS] = {
	{ .name = FW_FILE_ISP21XX, .segs = { 0x1000, 0 }, },
	{ .name = FW_FILE_ISP22XX, .segs = { 0x1000, 0 }, },
	{ .name = FW_FILE_ISP2300, .segs = { 0x800, 0 }, },
	{ .name = FW_FILE_ISP2322, .segs = { 0x800, 0x1c000, 0x1e000, 0 }, },
	{ .name = FW_FILE_ISP24XX, },
	{ .name = FW_FILE_ISP25XX, },
	{ .name = FW_FILE_ISP81XX, },
	{ .name = FW_FILE_ISP82XX, },
	{ .name = FW_FILE_ISP2031, },
	{ .name = FW_FILE_ISP8031, },
	{ .name = FW_FILE_ISP27XX, },
};

struct fw_blob *
qla2x00_request_firmware(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct fw_blob *blob;

	if (IS_QLA2100(ha)) {
		blob = &qla_fw_blobs[FW_ISP21XX];
	} else if (IS_QLA2200(ha)) {
		blob = &qla_fw_blobs[FW_ISP22XX];
	} else if (IS_QLA2300(ha) || IS_QLA2312(ha) || IS_QLA6312(ha)) {
		blob = &qla_fw_blobs[FW_ISP2300];
	} else if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
		blob = &qla_fw_blobs[FW_ISP2322];
	} else if (IS_QLA24XX_TYPE(ha)) {
		blob = &qla_fw_blobs[FW_ISP24XX];
	} else if (IS_QLA25XX(ha)) {
		blob = &qla_fw_blobs[FW_ISP25XX];
	} else if (IS_QLA81XX(ha)) {
		blob = &qla_fw_blobs[FW_ISP81XX];
	} else if (IS_QLA82XX(ha)) {
		blob = &qla_fw_blobs[FW_ISP82XX];
	} else if (IS_QLA2031(ha)) {
		blob = &qla_fw_blobs[FW_ISP2031];
	} else if (IS_QLA8031(ha)) {
		blob = &qla_fw_blobs[FW_ISP8031];
	} else if (IS_QLA27XX(ha)) {
		blob = &qla_fw_blobs[FW_ISP27XX];
	} else {
		return NULL;
	}

	mutex_lock(&qla_fw_lock);
	if (blob->fw)
		goto out;

	if (request_firmware(&blob->fw, blob->name, &ha->pdev->dev)) {
		ql_log(ql_log_warn, vha, 0x0063,
		    "Failed to load firmware image (%s).\n", blob->name);
		blob->fw = NULL;
		blob = NULL;
		goto out;
	}

out:
	mutex_unlock(&qla_fw_lock);
	return blob;
}

static void
qla2x00_release_firmware(void)
{
	int idx;

	mutex_lock(&qla_fw_lock);
	for (idx = 0; idx < FW_BLOBS; idx++)
		release_firmware(qla_fw_blobs[idx].fw);
	mutex_unlock(&qla_fw_lock);
}

static pci_ers_result_t
qla2xxx_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	scsi_qla_host_t *vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_aer, vha, 0x9000,
	    "PCI error detected, state %x.\n", state);

	if (!atomic_read(&pdev->enable_cnt)) {
		ql_log(ql_log_info, vha, 0xffff,
			"PCI device is disabled,state %x\n", state);
		return PCI_ERS_RESULT_NEED_RESET;
	}

	switch (state) {
	case pci_channel_io_normal:
		ha->flags.eeh_busy = 0;
		if (ql2xmqsupport || ql2xnvmeenable) {
			set_bit(QPAIR_ONLINE_CHECK_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		ha->flags.eeh_busy = 1;
		/* For ISP82XX complete any pending mailbox cmd */
		if (IS_QLA82XX(ha)) {
			ha->flags.isp82xx_fw_hung = 1;
			ql_dbg(ql_dbg_aer, vha, 0x9001, "Pci channel io frozen\n");
			qla82xx_clear_pending_mbx(vha);
		}
		qla2x00_free_irqs(vha);
		pci_disable_device(pdev);
		/* Return back all IOs */
		qla2x00_abort_all_cmds(vha, DID_RESET << 16);
		if (ql2xmqsupport || ql2xnvmeenable) {
			set_bit(QPAIR_ONLINE_CHECK_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		ha->flags.pci_channel_io_perm_failure = 1;
		qla2x00_abort_all_cmds(vha, DID_NO_CONNECT << 16);
		if (ql2xmqsupport || ql2xnvmeenable) {
			set_bit(QPAIR_ONLINE_CHECK_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t
qla2xxx_pci_mmio_enabled(struct pci_dev *pdev)
{
	int risc_paused = 0;
	uint32_t stat;
	unsigned long flags;
	scsi_qla_host_t *base_vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = base_vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	struct device_reg_24xx __iomem *reg24 = &ha->iobase->isp24;

	if (IS_QLA82XX(ha))
		return PCI_ERS_RESULT_RECOVERED;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (IS_QLA2100(ha) || IS_QLA2200(ha)){
		stat = RD_REG_DWORD(&reg->hccr);
		if (stat & HCCR_RISC_PAUSE)
			risc_paused = 1;
	} else if (IS_QLA23XX(ha)) {
		stat = RD_REG_DWORD(&reg->u.isp2300.host_status);
		if (stat & HSR_RISC_PAUSED)
			risc_paused = 1;
	} else if (IS_FWI2_CAPABLE(ha)) {
		stat = RD_REG_DWORD(&reg24->host_status);
		if (stat & HSRX_RISC_PAUSED)
			risc_paused = 1;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (risc_paused) {
		ql_log(ql_log_info, base_vha, 0x9003,
		    "RISC paused -- mmio_enabled, Dumping firmware.\n");
		ha->isp_ops->fw_dump(base_vha, 0);

		return PCI_ERS_RESULT_NEED_RESET;
	} else
		return PCI_ERS_RESULT_RECOVERED;
}

static uint32_t
qla82xx_error_recovery(scsi_qla_host_t *base_vha)
{
	uint32_t rval = QLA_FUNCTION_FAILED;
	uint32_t drv_active = 0;
	struct qla_hw_data *ha = base_vha->hw;
	int fn;
	struct pci_dev *other_pdev = NULL;

	ql_dbg(ql_dbg_aer, base_vha, 0x9006,
	    "Entered %s.\n", __func__);

	set_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);

	if (base_vha->flags.online) {
		/* Abort all outstanding commands,
		 * so as to be requeued later */
		qla2x00_abort_isp_cleanup(base_vha);
	}


	fn = PCI_FUNC(ha->pdev->devfn);
	while (fn > 0) {
		fn--;
		ql_dbg(ql_dbg_aer, base_vha, 0x9007,
		    "Finding pci device at function = 0x%x.\n", fn);
		other_pdev =
		    pci_get_domain_bus_and_slot(pci_domain_nr(ha->pdev->bus),
		    ha->pdev->bus->number, PCI_DEVFN(PCI_SLOT(ha->pdev->devfn),
		    fn));

		if (!other_pdev)
			continue;
		if (atomic_read(&other_pdev->enable_cnt)) {
			ql_dbg(ql_dbg_aer, base_vha, 0x9008,
			    "Found PCI func available and enable at 0x%x.\n",
			    fn);
			pci_dev_put(other_pdev);
			break;
		}
		pci_dev_put(other_pdev);
	}

	if (!fn) {
		/* Reset owner */
		ql_dbg(ql_dbg_aer, base_vha, 0x9009,
		    "This devfn is reset owner = 0x%x.\n",
		    ha->pdev->devfn);
		qla82xx_idc_lock(ha);

		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
		    QLA8XXX_DEV_INITIALIZING);

		qla82xx_wr_32(ha, QLA82XX_CRB_DRV_IDC_VERSION,
		    QLA82XX_IDC_VERSION);

		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
		ql_dbg(ql_dbg_aer, base_vha, 0x900a,
		    "drv_active = 0x%x.\n", drv_active);

		qla82xx_idc_unlock(ha);
		/* Reset if device is not already reset
		 * drv_active would be 0 if a reset has already been done
		 */
		if (drv_active)
			rval = qla82xx_start_firmware(base_vha);
		else
			rval = QLA_SUCCESS;
		qla82xx_idc_lock(ha);

		if (rval != QLA_SUCCESS) {
			ql_log(ql_log_info, base_vha, 0x900b,
			    "HW State: FAILED.\n");
			qla82xx_clear_drv_active(ha);
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			    QLA8XXX_DEV_FAILED);
		} else {
			ql_log(ql_log_info, base_vha, 0x900c,
			    "HW State: READY.\n");
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			    QLA8XXX_DEV_READY);
			qla82xx_idc_unlock(ha);
			ha->flags.isp82xx_fw_hung = 0;
			rval = qla82xx_restart_isp(base_vha);
			qla82xx_idc_lock(ha);
			/* Clear driver state register */
			qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, 0);
			qla82xx_set_drv_active(base_vha);
		}
		qla82xx_idc_unlock(ha);
	} else {
		ql_dbg(ql_dbg_aer, base_vha, 0x900d,
		    "This devfn is not reset owner = 0x%x.\n",
		    ha->pdev->devfn);
		if ((qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE) ==
		    QLA8XXX_DEV_READY)) {
			ha->flags.isp82xx_fw_hung = 0;
			rval = qla82xx_restart_isp(base_vha);
			qla82xx_idc_lock(ha);
			qla82xx_set_drv_active(base_vha);
			qla82xx_idc_unlock(ha);
		}
	}
	clear_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);

	return rval;
}

static pci_ers_result_t
qla2xxx_pci_slot_reset(struct pci_dev *pdev)
{
	pci_ers_result_t ret = PCI_ERS_RESULT_DISCONNECT;
	scsi_qla_host_t *base_vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = base_vha->hw;
	struct rsp_que *rsp;
	int rc, retries = 10;

	ql_dbg(ql_dbg_aer, base_vha, 0x9004,
	    "Slot Reset.\n");

	/* Workaround: qla2xxx driver which access hardware earlier
	 * needs error state to be pci_channel_io_online.
	 * Otherwise mailbox command timesout.
	 */
	pdev->error_state = pci_channel_io_normal;

	pci_restore_state(pdev);

	/* pci_restore_state() clears the saved_state flag of the device
	 * save restored state which resets saved_state flag
	 */
	pci_save_state(pdev);

	if (ha->mem_only)
		rc = pci_enable_device_mem(pdev);
	else
		rc = pci_enable_device(pdev);

	if (rc) {
		ql_log(ql_log_warn, base_vha, 0x9005,
		    "Can't re-enable PCI device after reset.\n");
		goto exit_slot_reset;
	}

	rsp = ha->rsp_q_map[0];
	if (qla2x00_request_irqs(ha, rsp))
		goto exit_slot_reset;

	if (ha->isp_ops->pci_config(base_vha))
		goto exit_slot_reset;

	if (IS_QLA82XX(ha)) {
		if (qla82xx_error_recovery(base_vha) == QLA_SUCCESS) {
			ret = PCI_ERS_RESULT_RECOVERED;
			goto exit_slot_reset;
		} else
			goto exit_slot_reset;
	}

	while (ha->flags.mbox_busy && retries--)
		msleep(1000);

	set_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
	if (ha->isp_ops->abort_isp(base_vha) == QLA_SUCCESS)
		ret =  PCI_ERS_RESULT_RECOVERED;
	clear_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);


exit_slot_reset:
	ql_dbg(ql_dbg_aer, base_vha, 0x900e,
	    "slot_reset return %x.\n", ret);

	return ret;
}

static void
qla2xxx_pci_resume(struct pci_dev *pdev)
{
	scsi_qla_host_t *base_vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = base_vha->hw;
	int ret;

	ql_dbg(ql_dbg_aer, base_vha, 0x900f,
	    "pci_resume.\n");

	ret = qla2x00_wait_for_hba_online(base_vha);
	if (ret != QLA_SUCCESS) {
		ql_log(ql_log_fatal, base_vha, 0x9002,
		    "The device failed to resume I/O from slot/link_reset.\n");
	}

	pci_cleanup_aer_uncorrect_error_status(pdev);

	ha->flags.eeh_busy = 0;
}

static int qla2xxx_map_queues(struct Scsi_Host *shost)
{
	int rc;
	scsi_qla_host_t *vha = (scsi_qla_host_t *)shost->hostdata;

	if (USER_CTRL_IRQ(vha->hw))
		rc = blk_mq_map_queues(&shost->tag_set);
	else
		rc = blk_mq_pci_map_queues(&shost->tag_set, vha->hw->pdev, 0);
	return rc;
}

static const struct pci_error_handlers qla2xxx_err_handler = {
	.error_detected = qla2xxx_pci_error_detected,
	.mmio_enabled = qla2xxx_pci_mmio_enabled,
	.slot_reset = qla2xxx_pci_slot_reset,
	.resume = qla2xxx_pci_resume,
};

static struct pci_device_id qla2xxx_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2100) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2200) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2300) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2312) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2322) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP6312) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP6322) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2422) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2432) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP8432) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP5422) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP5432) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2532) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2031) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP8001) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP8021) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP8031) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISPF001) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP8044) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2071) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2271) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2261) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, qla2xxx_pci_tbl);

static struct pci_driver qla2xxx_pci_driver = {
	.name		= QLA2XXX_DRIVER_NAME,
	.driver		= {
		.owner		= THIS_MODULE,
	},
	.id_table	= qla2xxx_pci_tbl,
	.probe		= qla2x00_probe_one,
	.remove		= qla2x00_remove_one,
	.shutdown	= qla2x00_shutdown,
	.err_handler	= &qla2xxx_err_handler,
};

static const struct file_operations apidev_fops = {
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
};

/**
 * qla2x00_module_init - Module initialization.
 **/
static int __init
qla2x00_module_init(void)
{
	int ret = 0;

	/* Allocate cache for SRBs. */
	srb_cachep = kmem_cache_create("qla2xxx_srbs", sizeof(srb_t), 0,
	    SLAB_HWCACHE_ALIGN, NULL);
	if (srb_cachep == NULL) {
		ql_log(ql_log_fatal, NULL, 0x0001,
		    "Unable to allocate SRB cache...Failing load!.\n");
		return -ENOMEM;
	}

	/* Initialize target kmem_cache and mem_pools */
	ret = qlt_init();
	if (ret < 0) {
		goto destroy_cache;
	} else if (ret > 0) {
		/*
		 * If initiator mode is explictly disabled by qlt_init(),
		 * prevent scsi_transport_fc.c:fc_scsi_scan_rport() from
		 * performing scsi_scan_target() during LOOP UP event.
		 */
		qla2xxx_transport_functions.disable_target_scan = 1;
		qla2xxx_transport_vport_functions.disable_target_scan = 1;
	}

	/* Derive version string. */
	strcpy(qla2x00_version_str, QLA2XXX_VERSION);
	if (ql2xextended_error_logging)
		strcat(qla2x00_version_str, "-debug");
	if (ql2xextended_error_logging == 1)
		ql2xextended_error_logging = QL_DBG_DEFAULT1_MASK;

	qla2xxx_transport_template =
	    fc_attach_transport(&qla2xxx_transport_functions);
	if (!qla2xxx_transport_template) {
		ql_log(ql_log_fatal, NULL, 0x0002,
		    "fc_attach_transport failed...Failing load!.\n");
		ret = -ENODEV;
		goto qlt_exit;
	}

	apidev_major = register_chrdev(0, QLA2XXX_APIDEV, &apidev_fops);
	if (apidev_major < 0) {
		ql_log(ql_log_fatal, NULL, 0x0003,
		    "Unable to register char device %s.\n", QLA2XXX_APIDEV);
	}

	qla2xxx_transport_vport_template =
	    fc_attach_transport(&qla2xxx_transport_vport_functions);
	if (!qla2xxx_transport_vport_template) {
		ql_log(ql_log_fatal, NULL, 0x0004,
		    "fc_attach_transport vport failed...Failing load!.\n");
		ret = -ENODEV;
		goto unreg_chrdev;
	}
	ql_log(ql_log_info, NULL, 0x0005,
	    "QLogic Fibre Channel HBA Driver: %s.\n",
	    qla2x00_version_str);
	ret = pci_register_driver(&qla2xxx_pci_driver);
	if (ret) {
		ql_log(ql_log_fatal, NULL, 0x0006,
		    "pci_register_driver failed...ret=%d Failing load!.\n",
		    ret);
		goto release_vport_transport;
	}
	return ret;

release_vport_transport:
	fc_release_transport(qla2xxx_transport_vport_template);

unreg_chrdev:
	if (apidev_major >= 0)
		unregister_chrdev(apidev_major, QLA2XXX_APIDEV);
	fc_release_transport(qla2xxx_transport_template);

qlt_exit:
	qlt_exit();

destroy_cache:
	kmem_cache_destroy(srb_cachep);
	return ret;
}

/**
 * qla2x00_module_exit - Module cleanup.
 **/
static void __exit
qla2x00_module_exit(void)
{
	unregister_chrdev(apidev_major, QLA2XXX_APIDEV);
	pci_unregister_driver(&qla2xxx_pci_driver);
	qla2x00_release_firmware();
	kmem_cache_destroy(srb_cachep);
	qlt_exit();
	if (ctx_cachep)
		kmem_cache_destroy(ctx_cachep);
	fc_release_transport(qla2xxx_transport_template);
	fc_release_transport(qla2xxx_transport_vport_template);
}

module_init(qla2x00_module_init);
module_exit(qla2x00_module_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic Fibre Channel HBA Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
MODULE_FIRMWARE(FW_FILE_ISP21XX);
MODULE_FIRMWARE(FW_FILE_ISP22XX);
MODULE_FIRMWARE(FW_FILE_ISP2300);
MODULE_FIRMWARE(FW_FILE_ISP2322);
MODULE_FIRMWARE(FW_FILE_ISP24XX);
MODULE_FIRMWARE(FW_FILE_ISP25XX);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
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
#include <linux/refcount.h>
#include <linux/crash_dump.h>
#include <linux/trace_events.h>
#include <linux/trace.h>

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

static struct trace_array *qla_trc_array;

int ql2xfulldump_on_mpifail;
module_param(ql2xfulldump_on_mpifail, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql2xfulldump_on_mpifail,
		 "Set this to take full dump on MPI hang.");

int ql2xenforce_iocb_limit = 1;
module_param(ql2xenforce_iocb_limit, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ql2xenforce_iocb_limit,
		 "Enforce IOCB throttling, to avoid FW congestion. (default: 1)");

/*
 * CT6 CTX allocation cache
 */
static struct kmem_cache *ctx_cachep;
/*
 * error level for logging
 */
uint ql_errlev = 0x8001;

int ql2xsecenable;
module_param(ql2xsecenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xsecenable,
	"Enable/disable security. 0(Default) - Security disabled. 1 - Security enabled.");

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
		"Default is 0 - no PLOGI. 1 - perform PLOGI.");

int ql2xloginretrycount;
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

int ql2xextended_error_logging_ktrace = 1;
module_param(ql2xextended_error_logging_ktrace, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xextended_error_logging_ktrace,
		"Same BIT definition as ql2xextended_error_logging, but used to control logging to kernel trace buffer (default=1).\n");

int ql2xshiftctondsd = 6;
module_param(ql2xshiftctondsd, int, S_IRUGO);
MODULE_PARM_DESC(ql2xshiftctondsd,
		"Set to control shifting of command type processing "
		"based on total number of SG elements.");

int ql2xfdmienable = 1;
module_param(ql2xfdmienable, int, S_IRUGO|S_IWUSR);
module_param_named(fdmi, ql2xfdmienable, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xfdmienable,
		"Enables FDMI registrations. "
		"0 - no FDMI registrations. "
		"1 - provide FDMI registrations (default).");

#define MAX_Q_DEPTH	64
static int ql2xmaxqdepth = MAX_Q_DEPTH;
module_param(ql2xmaxqdepth, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xmaxqdepth,
		"Maximum queue depth to set for each LUN. "
		"Default is 64.");

int ql2xenabledif = 2;
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

int ql2xiidmaenable = 1;
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

int ql2xgffidenable;
module_param(ql2xgffidenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xgffidenable,
		"Enables GFF_ID checks of port type. "
		"Default is 0 - Do not use GFF_ID information.");

int ql2xasynctmfenable = 1;
module_param(ql2xasynctmfenable, int, S_IRUGO);
MODULE_PARM_DESC(ql2xasynctmfenable,
		"Enables issue of TM IOCBs asynchronously via IOCB mechanism"
		"Default is 1 - Issue TM IOCBs via mailbox mechanism.");

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

int ql2xexlogins;
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

int ql2xfwholdabts;
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

static int ql2xprotmask;
module_param(ql2xprotmask, int, 0644);
MODULE_PARM_DESC(ql2xprotmask,
		 "Override DIF/DIX protection capabilities mask\n"
		 "Default is 0 which sets protection mask based on "
		 "capabilities reported by HBA firmware.\n");

static int ql2xprotguard;
module_param(ql2xprotguard, int, 0644);
MODULE_PARM_DESC(ql2xprotguard, "Override choice of DIX checksum\n"
		 "  0 -- Let HBA firmware decide\n"
		 "  1 -- Force T10 CRC\n"
		 "  2 -- Force IP checksum\n");

int ql2xdifbundlinginternalbuffers;
module_param(ql2xdifbundlinginternalbuffers, int, 0644);
MODULE_PARM_DESC(ql2xdifbundlinginternalbuffers,
    "Force using internal buffers for DIF information\n"
    "0 (Default). Based on check.\n"
    "1 Force using internal buffers\n");

int ql2xsmartsan;
module_param(ql2xsmartsan, int, 0444);
module_param_named(smartsan, ql2xsmartsan, int, 0444);
MODULE_PARM_DESC(ql2xsmartsan,
		"Send SmartSAN Management Attributes for FDMI Registration."
		" Default is 0 - No SmartSAN registration,"
		" 1 - Register SmartSAN Management Attributes.");

int ql2xrdpenable;
module_param(ql2xrdpenable, int, 0444);
module_param_named(rdpenable, ql2xrdpenable, int, 0444);
MODULE_PARM_DESC(ql2xrdpenable,
		"Enables RDP responses. "
		"0 - no RDP responses (default). "
		"1 - provide RDP responses.");
int ql2xabts_wait_nvme = 1;
module_param(ql2xabts_wait_nvme, int, 0444);
MODULE_PARM_DESC(ql2xabts_wait_nvme,
		 "To wait for ABTS response on I/O timeouts for NVMe. (default: 1)");


static u32 ql2xdelay_before_pci_error_handling = 5;
module_param(ql2xdelay_before_pci_error_handling, uint, 0644);
MODULE_PARM_DESC(ql2xdelay_before_pci_error_handling,
	"Number of seconds delayed before qla begin PCI error self-handling (default: 5).\n");

static void qla2x00_clear_drv_active(struct qla_hw_data *);
static void qla2x00_free_device(scsi_qla_host_t *);
static void qla2xxx_map_queues(struct Scsi_Host *shost);
static void qla2x00_destroy_deferred_work(struct qla_hw_data *);

u32 ql2xnvme_queues = DEF_NVME_HW_QUEUES;
module_param(ql2xnvme_queues, uint, S_IRUGO);
MODULE_PARM_DESC(ql2xnvme_queues,
	"Number of NVMe Queues that can be configured.\n"
	"Final value will be min(ql2xnvme_queues, num_cpus,num_chip_queues)\n"
	"1 - Minimum number of queues supported\n"
	"8 - Default value");

int ql2xfc2target = 1;
module_param(ql2xfc2target, int, 0444);
MODULE_PARM_DESC(qla2xfc2target,
		  "Enables FC2 Target support. "
		  "0 - FC2 Target support is disabled. "
		  "1 - FC2 Target support is enabled (default).");

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
	ha->base_qpair->hw = ha;
	ha->base_qpair->req = req;
	ha->base_qpair->rsp = rsp;
	ha->base_qpair->vha = vha;
	ha->base_qpair->qp_lock_ptr = &ha->hardware_lock;
	ha->base_qpair->use_shadow_reg = IS_SHADOW_REG_CAPABLE(ha) ? 1 : 0;
	ha->base_qpair->msix = &ha->msix_entries[QLA_MSIX_RSP_Q];
	ha->base_qpair->srb_mempool = ha->srb_mempool;
	INIT_LIST_HEAD(&ha->base_qpair->hints_list);
	ha->base_qpair->enable_class_2 = ql2xenableclass2;
	/* init qpair to this cpu. Will adjust at run time. */
	qla_cpu_update(rsp->qpair, raw_smp_processor_id());
	ha->base_qpair->pdev = ha->pdev;

	if (IS_QLA27XX(ha) || IS_QLA83XX(ha) || IS_QLA28XX(ha))
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
qla2x00_pci_info_str(struct scsi_qla_host *vha, char *str, size_t str_len)
{
	struct qla_hw_data *ha = vha->hw;
	static const char *const pci_bus_modes[] = {
		"33", "66", "100", "133",
	};
	uint16_t pci_bus;

	pci_bus = (ha->pci_attr & (BIT_9 | BIT_10)) >> 9;
	if (pci_bus) {
		snprintf(str, str_len, "PCI-X (%s MHz)",
			 pci_bus_modes[pci_bus]);
	} else {
		pci_bus = (ha->pci_attr & BIT_8) >> 8;
		snprintf(str, str_len, "PCI (%s MHz)", pci_bus_modes[pci_bus]);
	}

	return str;
}

static char *
qla24xx_pci_info_str(struct scsi_qla_host *vha, char *str, size_t str_len)
{
	static const char *const pci_bus_modes[] = {
		"33", "66", "100", "133",
	};
	struct qla_hw_data *ha = vha->hw;
	uint32_t pci_bus;

	if (pci_is_pcie(ha->pdev)) {
		uint32_t lstat, lspeed, lwidth;
		const char *speed_str;

		pcie_capability_read_dword(ha->pdev, PCI_EXP_LNKCAP, &lstat);
		lspeed = lstat & PCI_EXP_LNKCAP_SLS;
		lwidth = (lstat & PCI_EXP_LNKCAP_MLW) >> 4;

		switch (lspeed) {
		case 1:
			speed_str = "2.5GT/s";
			break;
		case 2:
			speed_str = "5.0GT/s";
			break;
		case 3:
			speed_str = "8.0GT/s";
			break;
		case 4:
			speed_str = "16.0GT/s";
			break;
		default:
			speed_str = "<unknown>";
			break;
		}
		snprintf(str, str_len, "PCIe (%s x%d)", speed_str, lwidth);

		return str;
	}

	pci_bus = (ha->pci_attr & CSRX_PCIX_BUS_MODE_MASK) >> 8;
	if (pci_bus == 0 || pci_bus == 8)
		snprintf(str, str_len, "PCI (%s MHz)",
			 pci_bus_modes[pci_bus >> 3]);
	else
		snprintf(str, str_len, "PCI-X Mode %d (%s MHz)",
			 pci_bus & 4 ? 2 : 1,
			 pci_bus_modes[pci_bus & 3]);

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

void qla2x00_sp_free_dma(srb_t *sp)
{
	struct qla_hw_data *ha = sp->vha->hw;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);

	if (sp->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		sp->flags &= ~SRB_DMA_VALID;
	}

	if (sp->flags & SRB_CRC_PROT_DMA_VALID) {
		dma_unmap_sg(&ha->pdev->dev, scsi_prot_sglist(cmd),
		    scsi_prot_sg_count(cmd), cmd->sc_data_direction);
		sp->flags &= ~SRB_CRC_PROT_DMA_VALID;
	}

	if (sp->flags & SRB_CRC_CTX_DSD_VALID) {
		/* List assured to be having elements */
		qla2x00_clean_dsd_pool(ha, sp->u.scmd.crc_ctx);
		sp->flags &= ~SRB_CRC_CTX_DSD_VALID;
	}

	if (sp->flags & SRB_CRC_CTX_DMA_VALID) {
		struct crc_context *ctx0 = sp->u.scmd.crc_ctx;

		dma_pool_free(ha->dl_dma_pool, ctx0, ctx0->crc_ctx_dma);
		sp->flags &= ~SRB_CRC_CTX_DMA_VALID;
	}

	if (sp->flags & SRB_FCP_CMND_DMA_VALID) {
		struct ct6_dsd *ctx1 = sp->u.scmd.ct6_ctx;

		dma_pool_free(ha->fcp_cmnd_dma_pool, ctx1->fcp_cmnd,
		    ctx1->fcp_cmnd_dma);
		list_splice(&ctx1->dsd_list, &ha->gbl_dsd_list);
		ha->gbl_dsd_inuse -= ctx1->dsd_use_cnt;
		ha->gbl_dsd_avail += ctx1->dsd_use_cnt;
		mempool_free(ctx1, ha->ctx_mempool);
	}
}

void qla2x00_sp_compl(srb_t *sp, int res)
{
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	struct completion *comp = sp->comp;

	/* kref: INIT */
	kref_put(&sp->cmd_kref, qla2x00_sp_release);
	cmd->result = res;
	sp->type = 0;
	scsi_done(cmd);
	if (comp)
		complete(comp);
}

void qla2xxx_qpair_sp_free_dma(srb_t *sp)
{
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	struct qla_hw_data *ha = sp->fcport->vha->hw;

	if (sp->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		sp->flags &= ~SRB_DMA_VALID;
	}

	if (sp->flags & SRB_CRC_PROT_DMA_VALID) {
		dma_unmap_sg(&ha->pdev->dev, scsi_prot_sglist(cmd),
		    scsi_prot_sg_count(cmd), cmd->sc_data_direction);
		sp->flags &= ~SRB_CRC_PROT_DMA_VALID;
	}

	if (sp->flags & SRB_CRC_CTX_DSD_VALID) {
		/* List assured to be having elements */
		qla2x00_clean_dsd_pool(ha, sp->u.scmd.crc_ctx);
		sp->flags &= ~SRB_CRC_CTX_DSD_VALID;
	}

	if (sp->flags & SRB_DIF_BUNDL_DMA_VALID) {
		struct crc_context *difctx = sp->u.scmd.crc_ctx;
		struct dsd_dma *dif_dsd, *nxt_dsd;

		list_for_each_entry_safe(dif_dsd, nxt_dsd,
		    &difctx->ldif_dma_hndl_list, list) {
			list_del(&dif_dsd->list);
			dma_pool_free(ha->dif_bundl_pool, dif_dsd->dsd_addr,
			    dif_dsd->dsd_list_dma);
			kfree(dif_dsd);
			difctx->no_dif_bundl--;
		}

		list_for_each_entry_safe(dif_dsd, nxt_dsd,
		    &difctx->ldif_dsd_list, list) {
			list_del(&dif_dsd->list);
			dma_pool_free(ha->dl_dma_pool, dif_dsd->dsd_addr,
			    dif_dsd->dsd_list_dma);
			kfree(dif_dsd);
			difctx->no_ldif_dsd--;
		}

		if (difctx->no_ldif_dsd) {
			ql_dbg(ql_dbg_tgt+ql_dbg_verbose, sp->vha, 0xe022,
			    "%s: difctx->no_ldif_dsd=%x\n",
			    __func__, difctx->no_ldif_dsd);
		}

		if (difctx->no_dif_bundl) {
			ql_dbg(ql_dbg_tgt+ql_dbg_verbose, sp->vha, 0xe022,
			    "%s: difctx->no_dif_bundl=%x\n",
			    __func__, difctx->no_dif_bundl);
		}
		sp->flags &= ~SRB_DIF_BUNDL_DMA_VALID;
	}

	if (sp->flags & SRB_FCP_CMND_DMA_VALID) {
		struct ct6_dsd *ctx1 = sp->u.scmd.ct6_ctx;

		dma_pool_free(ha->fcp_cmnd_dma_pool, ctx1->fcp_cmnd,
		    ctx1->fcp_cmnd_dma);
		list_splice(&ctx1->dsd_list, &ha->gbl_dsd_list);
		ha->gbl_dsd_inuse -= ctx1->dsd_use_cnt;
		ha->gbl_dsd_avail += ctx1->dsd_use_cnt;
		mempool_free(ctx1, ha->ctx_mempool);
		sp->flags &= ~SRB_FCP_CMND_DMA_VALID;
	}

	if (sp->flags & SRB_CRC_CTX_DMA_VALID) {
		struct crc_context *ctx0 = sp->u.scmd.crc_ctx;

		dma_pool_free(ha->dl_dma_pool, ctx0, ctx0->crc_ctx_dma);
		sp->flags &= ~SRB_CRC_CTX_DMA_VALID;
	}
}

void qla2xxx_qpair_sp_compl(srb_t *sp, int res)
{
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	struct completion *comp = sp->comp;

	/* ref: INIT */
	kref_put(&sp->cmd_kref, qla2x00_sp_release);
	cmd->result = res;
	sp->type = 0;
	scsi_done(cmd);
	if (comp)
		complete(comp);
}

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

	if (unlikely(test_bit(UNLOADING, &base_vha->dpc_flags)) ||
	    WARN_ON_ONCE(!rport)) {
		cmd->result = DID_NO_CONNECT << 16;
		goto qc24_fail_command;
	}

	if (ha->mqenable) {
		uint32_t tag;
		uint16_t hwq;
		struct qla_qpair *qpair = NULL;

		tag = blk_mq_unique_tag(scsi_cmd_to_rq(cmd));
		hwq = blk_mq_unique_tag_to_hwq(tag);
		qpair = ha->queue_pair_map[hwq];

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

	if (!fcport || fcport->deleted) {
		cmd->result = DID_IMM_RETRY << 16;
		goto qc24_fail_command;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE || fcport->deleted) {
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

	sp = scsi_cmd_priv(cmd);
	/* ref: INIT */
	qla2xxx_init_sp(sp, vha, vha->hw->base_qpair, fcport);

	sp->u.scmd.cmd = cmd;
	sp->type = SRB_SCSI_CMD;
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
	/* ref: INIT */
	kref_put(&sp->cmd_kref, qla2x00_sp_release);

qc24_target_busy:
	return SCSI_MLQUEUE_TARGET_BUSY;

qc24_fail_command:
	scsi_done(cmd);

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

	rval = rport ? fc_remote_port_chkready(rport) : (DID_NO_CONNECT << 16);
	if (rval) {
		cmd->result = rval;
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x3076,
		    "fc_remote_port_chkready failed for cmd=%p, rval=0x%x.\n",
		    cmd, rval);
		goto qc24_fail_command;
	}

	if (!qpair->online) {
		ql_dbg(ql_dbg_io, vha, 0x3077,
		       "qpair not online. eeh_busy=%d.\n", ha->flags.eeh_busy);
		cmd->result = DID_NO_CONNECT << 16;
		goto qc24_fail_command;
	}

	if (!fcport || fcport->deleted) {
		cmd->result = DID_IMM_RETRY << 16;
		goto qc24_fail_command;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE || fcport->deleted) {
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

	sp = scsi_cmd_priv(cmd);
	/* ref: INIT */
	qla2xxx_init_sp(sp, vha, qpair, fcport);

	sp->u.scmd.cmd = cmd;
	sp->type = SRB_SCSI_CMD;
	sp->free = qla2xxx_qpair_sp_free_dma;
	sp->done = qla2xxx_qpair_sp_compl;

	rval = ha->isp_ops->start_scsi_mq(sp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x3078,
		    "Start scsi failed rval=%d for cmd=%p.\n", rval, cmd);
		goto qc24_host_busy_free_sp;
	}

	return 0;

qc24_host_busy_free_sp:
	/* ref: INIT */
	kref_put(&sp->cmd_kref, qla2x00_sp_release);

qc24_target_busy:
	return SCSI_MLQUEUE_TARGET_BUSY;

qc24_fail_command:
	scsi_done(cmd);

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
 *    Completed in time : QLA_SUCCESS
 *    Did not complete in time : QLA_FUNCTION_FAILED
 */
static int
qla2x00_eh_wait_on_command(struct scsi_cmnd *cmd)
{
#define ABORT_POLLING_PERIOD	1000
#define ABORT_WAIT_ITER		((2 * 1000) / (ABORT_POLLING_PERIOD))
	unsigned long wait_iter = ABORT_WAIT_ITER;
	scsi_qla_host_t *vha = shost_priv(cmd->device->host);
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp = scsi_cmd_priv(cmd);
	int ret = QLA_SUCCESS;

	if (unlikely(pci_channel_offline(ha->pdev)) || ha->flags.eeh_busy) {
		ql_dbg(ql_dbg_taskm, vha, 0x8005,
		    "Return:eh_wait.\n");
		return ret;
	}

	while (sp->type && wait_iter--)
		msleep(ABORT_POLLING_PERIOD);
	if (sp->type)
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
	/* Return 0 = sleep, x=wake */

	spin_lock_irqsave(&ha->tgt.sess_lock, flags);
	ql_dbg(ql_dbg_init, vha, 0x00ec,
	    "tgt %p, fcport_count=%d\n",
	    vha, vha->fcport_count);
	res = (vha->fcport_count == 0);
	if  (res) {
		struct fc_port *fcport;

		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if (fcport->deleted != QLA_SESS_DELETED) {
				/* session(s) may not be fully logged in
				 * (ie fcport_count=0), but session
				 * deletion thread(s) may be inflight.
				 */

				res = 0;
				break;
			}
		}
	}
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
	u8 i;

	qla2x00_mark_all_devices_lost(vha);

	for (i = 0; i < 10; i++) {
		if (wait_event_timeout(vha->fcport_waitQ,
		    test_fcport_count(vha), HZ) > 0)
			break;
	}

	flush_workqueue(vha->hw->wq);
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
	DECLARE_COMPLETION_ONSTACK(comp);
	srb_t *sp;
	int ret;
	unsigned int id;
	uint64_t lun;
	int rval;
	struct qla_hw_data *ha = vha->hw;
	uint32_t ratov_j;
	struct qla_qpair *qpair;
	unsigned long flags;
	int fast_fail_status = SUCCESS;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x8042,
		    "PCI/Register disconnect, exiting.\n");
		qla_pci_set_eeh_busy(vha);
		return FAILED;
	}

	/* Save any FAST_IO_FAIL value to return later if abort succeeds */
	ret = fc_block_scsi_eh(cmd);
	if (ret != 0)
		fast_fail_status = ret;

	sp = scsi_cmd_priv(cmd);
	qpair = sp->qpair;

	vha->cmd_timeout_cnt++;

	if ((sp->fcport && sp->fcport->deleted) || !qpair)
		return fast_fail_status != SUCCESS ? fast_fail_status : FAILED;

	spin_lock_irqsave(qpair->qp_lock_ptr, flags);
	sp->comp = &comp;
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);


	id = cmd->device->id;
	lun = cmd->device->lun;

	ql_dbg(ql_dbg_taskm, vha, 0x8002,
	    "Aborting from RISC nexus=%ld:%d:%llu sp=%p cmd=%p handle=%x\n",
	    vha->host_no, id, lun, sp, cmd, sp->handle);

	/*
	 * Abort will release the original Command/sp from FW. Let the
	 * original command call scsi_done. In return, he will wakeup
	 * this sleeping thread.
	 */
	rval = ha->isp_ops->abort_command(sp);

	ql_dbg(ql_dbg_taskm, vha, 0x8003,
	       "Abort command mbx cmd=%p, rval=%x.\n", cmd, rval);

	/* Wait for the command completion. */
	ratov_j = ha->r_a_tov/10 * 4 * 1000;
	ratov_j = msecs_to_jiffies(ratov_j);
	switch (rval) {
	case QLA_SUCCESS:
		if (!wait_for_completion_timeout(&comp, ratov_j)) {
			ql_dbg(ql_dbg_taskm, vha, 0xffff,
			    "%s: Abort wait timer (4 * R_A_TOV[%d]) expired\n",
			    __func__, ha->r_a_tov/10);
			ret = FAILED;
		} else {
			ret = fast_fail_status;
		}
		break;
	default:
		ret = FAILED;
		break;
	}

	sp->comp = NULL;

	ql_log(ql_log_info, vha, 0x801c,
	    "Abort command issued nexus=%ld:%d:%llu -- %x.\n",
	    vha->host_no, id, lun, ret);

	return ret;
}

/*
 * Returns: QLA_SUCCESS or QLA_FUNCTION_FAILED.
 */
static int
__qla2x00_eh_wait_for_pending_commands(struct qla_qpair *qpair, unsigned int t,
				       uint64_t l, enum nexus_wait_type type)
{
	int cnt, match, status;
	unsigned long flags;
	scsi_qla_host_t *vha = qpair->vha;
	struct req_que *req = qpair->req;
	srb_t *sp;
	struct scsi_cmnd *cmd;

	status = QLA_SUCCESS;

	spin_lock_irqsave(qpair->qp_lock_ptr, flags);
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

		spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);
		status = qla2x00_eh_wait_on_command(cmd);
		spin_lock_irqsave(qpair->qp_lock_ptr, flags);
	}
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	return status;
}

int
qla2x00_eh_wait_for_pending_commands(scsi_qla_host_t *vha, unsigned int t,
				     uint64_t l, enum nexus_wait_type type)
{
	struct qla_qpair *qpair;
	struct qla_hw_data *ha = vha->hw;
	int i, status = QLA_SUCCESS;

	status = __qla2x00_eh_wait_for_pending_commands(ha->base_qpair, t, l,
							type);
	for (i = 0; status == QLA_SUCCESS && i < ha->max_qpairs; i++) {
		qpair = ha->queue_pair_map[i];
		if (!qpair)
			continue;
		status = __qla2x00_eh_wait_for_pending_commands(qpair, t, l,
								type);
	}
	return status;
}

static char *reset_errors[] = {
	"HBA not online",
	"HBA not ready",
	"Task management failed",
	"Waiting for command completions",
};

static int
qla2xxx_eh_device_reset(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	scsi_qla_host_t *vha = shost_priv(sdev->host);
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	fc_port_t *fcport = (struct fc_port *) sdev->hostdata;
	struct qla_hw_data *ha = vha->hw;
	int err;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x803e,
		    "PCI/Register disconnect, exiting.\n");
		qla_pci_set_eeh_busy(vha);
		return FAILED;
	}

	if (!fcport) {
		return FAILED;
	}

	err = fc_block_rport(rport);
	if (err != 0)
		return err;

	if (fcport->deleted)
		return FAILED;

	ql_log(ql_log_info, vha, 0x8009,
	    "DEVICE RESET ISSUED nexus=%ld:%d:%llu cmd=%p.\n", vha->host_no,
	    sdev->id, sdev->lun, cmd);

	err = 0;
	if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800a,
		    "Wait for hba online failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}
	err = 2;
	if (ha->isp_ops->lun_reset(fcport, sdev->lun, 1)
		!= QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800c,
		    "do_reset failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}
	err = 3;
	if (qla2x00_eh_wait_for_pending_commands(vha, sdev->id,
	    sdev->lun, WAIT_LUN) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800d,
		    "wait for pending cmds failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}

	ql_log(ql_log_info, vha, 0x800e,
	    "DEVICE RESET SUCCEEDED nexus:%ld:%d:%llu cmd=%p.\n",
	    vha->host_no, sdev->id, sdev->lun, cmd);

	return SUCCESS;

eh_reset_failed:
	ql_log(ql_log_info, vha, 0x800f,
	    "DEVICE RESET FAILED: %s nexus=%ld:%d:%llu cmd=%p.\n",
	    reset_errors[err], vha->host_no, sdev->id, sdev->lun,
	    cmd);
	vha->reset_cmd_err_cnt++;
	return FAILED;
}

static int
qla2xxx_eh_target_reset(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	scsi_qla_host_t *vha = shost_priv(rport_to_shost(rport));
	struct qla_hw_data *ha = vha->hw;
	fc_port_t *fcport = *(fc_port_t **)rport->dd_data;
	int err;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x803f,
		    "PCI/Register disconnect, exiting.\n");
		qla_pci_set_eeh_busy(vha);
		return FAILED;
	}

	if (!fcport) {
		return FAILED;
	}

	err = fc_block_rport(rport);
	if (err != 0)
		return err;

	if (fcport->deleted)
		return FAILED;

	ql_log(ql_log_info, vha, 0x8009,
	    "TARGET RESET ISSUED nexus=%ld:%d cmd=%p.\n", vha->host_no,
	    sdev->id, cmd);

	err = 0;
	if (qla2x00_wait_for_hba_online(vha) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800a,
		    "Wait for hba online failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}
	err = 2;
	if (ha->isp_ops->target_reset(fcport, 0, 0) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800c,
		    "target_reset failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}
	err = 3;
	if (qla2x00_eh_wait_for_pending_commands(vha, sdev->id,
	    0, WAIT_TARGET) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x800d,
		    "wait for pending cmds failed for cmd=%p.\n", cmd);
		goto eh_reset_failed;
	}

	ql_log(ql_log_info, vha, 0x800e,
	    "TARGET RESET SUCCEEDED nexus:%ld:%d cmd=%p.\n",
	    vha->host_no, sdev->id, cmd);

	return SUCCESS;

eh_reset_failed:
	ql_log(ql_log_info, vha, 0x800f,
	    "TARGET RESET FAILED: %s nexus=%ld:%d:%llu cmd=%p.\n",
	    reset_errors[err], vha->host_no, cmd->device->id, cmd->device->lun,
	    cmd);
	vha->reset_cmd_err_cnt++;
	return FAILED;
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
	int ret = FAILED;
	unsigned int id;
	uint64_t lun;
	struct qla_hw_data *ha = vha->hw;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, vha, 0x8040,
		    "PCI/Register disconnect, exiting.\n");
		qla_pci_set_eeh_busy(vha);
		return FAILED;
	}

	id = cmd->device->id;
	lun = cmd->device->lun;

	if (qla2x00_chip_is_down(vha))
		return ret;

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
		qla_pci_set_eeh_busy(vha);
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
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLAFX00(ha))
		return QLA_SUCCESS;

	if (ha->flags.enable_lip_full_login && !IS_CNA_CAPABLE(ha)) {
		atomic_set(&vha->loop_state, LOOP_DOWN);
		atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
		qla2x00_mark_all_devices_lost(vha);
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

/*
 * The caller must ensure that no completion interrupts will happen
 * while this function is in progress.
 */
static void qla2x00_abort_srb(struct qla_qpair *qp, srb_t *sp, const int res,
			      unsigned long *flags)
	__releases(qp->qp_lock_ptr)
	__acquires(qp->qp_lock_ptr)
{
	DECLARE_COMPLETION_ONSTACK(comp);
	scsi_qla_host_t *vha = qp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	int rval;
	bool ret_cmd;
	uint32_t ratov_j;

	lockdep_assert_held(qp->qp_lock_ptr);

	if (qla2x00_chip_is_down(vha)) {
		sp->done(sp, res);
		return;
	}

	if (sp->type == SRB_NVME_CMD || sp->type == SRB_NVME_LS ||
	    (sp->type == SRB_SCSI_CMD && !ha->flags.eeh_busy &&
	     !test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) &&
	     !qla2x00_isp_reg_stat(ha))) {
		if (sp->comp) {
			sp->done(sp, res);
			return;
		}

		sp->comp = &comp;
		spin_unlock_irqrestore(qp->qp_lock_ptr, *flags);

		rval = ha->isp_ops->abort_command(sp);
		/* Wait for command completion. */
		ret_cmd = false;
		ratov_j = ha->r_a_tov/10 * 4 * 1000;
		ratov_j = msecs_to_jiffies(ratov_j);
		switch (rval) {
		case QLA_SUCCESS:
			if (wait_for_completion_timeout(&comp, ratov_j)) {
				ql_dbg(ql_dbg_taskm, vha, 0xffff,
				    "%s: Abort wait timer (4 * R_A_TOV[%d]) expired\n",
				    __func__, ha->r_a_tov/10);
				ret_cmd = true;
			}
			/* else FW return SP to driver */
			break;
		default:
			ret_cmd = true;
			break;
		}

		spin_lock_irqsave(qp->qp_lock_ptr, *flags);
		if (ret_cmd && blk_mq_request_started(scsi_cmd_to_rq(cmd)))
			sp->done(sp, res);
	} else {
		sp->done(sp, res);
	}
}

/*
 * The caller must ensure that no completion interrupts will happen
 * while this function is in progress.
 */
static void
__qla2x00_abort_all_cmds(struct qla_qpair *qp, int res)
{
	int cnt;
	unsigned long flags;
	srb_t *sp;
	scsi_qla_host_t *vha = qp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;
	struct qla_tgt *tgt = vha->vha_tgt.qla_tgt;
	struct qla_tgt_cmd *cmd;

	if (!ha->req_q_map)
		return;
	spin_lock_irqsave(qp->qp_lock_ptr, flags);
	req = qp->req;
	for (cnt = 1; cnt < req->num_outstanding_cmds; cnt++) {
		sp = req->outstanding_cmds[cnt];
		if (sp) {
			/*
			 * perform lockless completion during driver unload
			 */
			if (qla2x00_chip_is_down(vha)) {
				req->outstanding_cmds[cnt] = NULL;
				spin_unlock_irqrestore(qp->qp_lock_ptr, flags);
				sp->done(sp, res);
				spin_lock_irqsave(qp->qp_lock_ptr, flags);
				continue;
			}

			switch (sp->cmd_type) {
			case TYPE_SRB:
				qla2x00_abort_srb(qp, sp, res, &flags);
				break;
			case TYPE_TGT_CMD:
				if (!vha->hw->tgt.tgt_ops || !tgt ||
				    qla_ini_mode_enabled(vha)) {
					ql_dbg(ql_dbg_tgt_mgt, vha, 0xf003,
					    "HOST-ABORT-HNDLR: dpc_flags=%lx. Target mode disabled\n",
					    vha->dpc_flags);
					continue;
				}
				cmd = (struct qla_tgt_cmd *)sp;
				cmd->aborted = 1;
				break;
			case TYPE_TGT_TMCMD:
				/* Skip task management functions. */
				break;
			default:
				break;
			}
			req->outstanding_cmds[cnt] = NULL;
		}
	}
	spin_unlock_irqrestore(qp->qp_lock_ptr, flags);
}

/*
 * The caller must ensure that no completion interrupts will happen
 * while this function is in progress.
 */
void
qla2x00_abort_all_cmds(scsi_qla_host_t *vha, int res)
{
	int que;
	struct qla_hw_data *ha = vha->hw;

	/* Continue only if initialization complete. */
	if (!ha->base_qpair)
		return;
	__qla2x00_abort_all_cmds(ha->base_qpair, res);

	if (!ha->queue_pair_map)
		return;
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
		    !dma_set_coherent_mask(&ha->pdev->dev, DMA_BIT_MASK(64))) {
			/* Ok, a 64bit DMA mask is applicable. */
			ha->flags.enable_64bit_addressing = 1;
			ha->isp_ops->calc_req_entries = qla2x00_calc_iocbs_64;
			ha->isp_ops->build_iocbs = qla2x00_build_scsi_iocbs_64;
			return;
		}
	}

	dma_set_mask(&ha->pdev->dev, DMA_BIT_MASK(32));
	dma_set_coherent_mask(&ha->pdev->dev, DMA_BIT_MASK(32));
}

static void
qla2x00_enable_intrs(struct qla_hw_data *ha)
{
	unsigned long flags = 0;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 1;
	/* enable risc and host interrupts */
	wrt_reg_word(&reg->ictrl, ICR_EN_INT | ICR_EN_RISC);
	rd_reg_word(&reg->ictrl);
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
	wrt_reg_word(&reg->ictrl, 0);
	rd_reg_word(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void
qla24xx_enable_intrs(struct qla_hw_data *ha)
{
	unsigned long flags = 0;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 1;
	wrt_reg_dword(&reg->ictrl, ICRX_EN_RISC_INT);
	rd_reg_dword(&reg->ictrl);
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
	wrt_reg_dword(&reg->ictrl, 0);
	rd_reg_dword(&reg->ictrl);
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

	/* Check if FW supports MQ or not */
	if (!(ha->fw_attributes & BIT_6))
		goto mqiobase_exit;

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
	.update_fw_options	= qla24xx_update_fw_options,
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
	.update_fw_options	= qla24xx_update_fw_options,
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
	.update_fw_options	= qla24xx_update_fw_options,
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
	.mpi_fw_dump		= qla27xx_mpi_fwdump,
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
	case PCI_DEVICE_ID_QLOGIC_ISP2081:
	case PCI_DEVICE_ID_QLOGIC_ISP2089:
		ha->isp_type |= DT_ISP2081;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->device_type |= DT_T10_PI;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2281:
	case PCI_DEVICE_ID_QLOGIC_ISP2289:
		ha->isp_type |= DT_ISP2281;
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
		if (IS_QLA25XX(ha) || IS_QLA2031(ha) ||
		    IS_QLA27XX(ha) || IS_QLA28XX(ha))
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

static void qla_heartbeat_work_fn(struct work_struct *work)
{
	struct qla_hw_data *ha = container_of(work,
		struct qla_hw_data, heartbeat_work);
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	if (!ha->flags.mbox_busy && base_vha->flags.init_done)
		qla_no_op_mb(base_vha);
}

static void qla2x00_iocb_work_fn(struct work_struct *work)
{
	struct scsi_qla_host *vha = container_of(work,
		struct scsi_qla_host, iocb_work);
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	int i = 2;
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

static void
qla_trace_init(void)
{
	qla_trc_array = trace_array_get_by_name("qla2xxx");
	if (!qla_trc_array) {
		ql_log(ql_log_fatal, NULL, 0x0001,
		       "Unable to create qla2xxx trace instance, instance logging will be disabled.\n");
		return;
	}

	QLA_TRACE_ENABLE(qla_trc_array);
}

static void
qla_trace_uninit(void)
{
	if (!qla_trc_array)
		return;
	trace_array_put(qla_trc_array);
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
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2261 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2081 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2281 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2089 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2289) {
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

	if (is_kdump_kernel()) {
		ql2xmqsupport = 0;
		ql2xallocfwdump = 0;
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

	spin_lock_init(&ha->sadb_lock);
	INIT_LIST_HEAD(&ha->sadb_tx_index_list);
	INIT_LIST_HEAD(&ha->sadb_rx_index_list);

	spin_lock_init(&ha->sadb_fp_lock);

	if (qla_edif_sadb_build_free_pool(ha)) {
		kfree(ha);
		goto  disable_device;
	}

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
	    IS_QLA83XX(ha) || IS_QLA27XX(ha) || IS_QLA28XX(ha))
		pdev->needs_freset = 1;

	ha->prev_topology = 0;
	ha->init_cb_size = sizeof(init_cb_t);
	ha->link_data_rate = PORT_SPEED_UNKNOWN;
	ha->optrom_size = OPTROM_SIZE_2300;
	ha->max_exchg = FW_MAX_EXCHANGES_CNT;
	atomic_set(&ha->num_pend_mbx_stage1, 0);
	atomic_set(&ha->num_pend_mbx_stage2, 0);
	atomic_set(&ha->num_pend_mbx_stage3, 0);
	atomic_set(&ha->zio_threshold, DEFAULT_ZIO_THRESHOLD);
	ha->last_zio_threshold = DEFAULT_ZIO_THRESHOLD;

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
	} else if (IS_QLA28XX(ha)) {
		ha->portnum = PCI_FUNC(ha->pdev->devfn);
		ha->max_fibre_devices = MAX_FIBRE_DEVICES_2400;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		req_length = REQUEST_ENTRY_CNT_83XX;
		rsp_length = RESPONSE_ENTRY_CNT_83XX;
		ha->tgt.atio_q_length = ATIO_ENTRY_CNT_24XX;
		ha->max_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_81xx);
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_28XX;
		ha->nvram_npiv_size = QLA_MAX_VPORTS_QLA25XX;
		ha->isp_ops = &qla27xx_isp_ops;
		ha->flash_conf_off = FARX_ACCESS_FLASH_CONF_28XX;
		ha->flash_data_off = FARX_ACCESS_FLASH_DATA_28XX;
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

	qla2xxx_reset_stats(host, QLA2XX_HW_ERROR | QLA2XX_SHT_LNK_DWN |
			    QLA2XX_INT_ERR | QLA2XX_CMD_TIMEOUT |
			    QLA2XX_RESET_CMD_ERR | QLA2XX_TGT_SHT_LNK_DOWN);

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
	INIT_WORK(&ha->heartbeat_work, qla_heartbeat_work_fn);

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
		ret = -ENODEV;
		goto probe_failed;
	}

	if (ha->mqenable) {
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
	if (ha->mqenable || IS_QLA83XX(ha) || IS_QLA27XX(ha) ||
	    IS_QLA28XX(ha)) {
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

	ha->wq = alloc_workqueue("qla2xxx_wq", WQ_MEM_RECLAIM, 0);
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

	/* Check if FW supports MQ or not for ISP25xx */
	if (IS_QLA25XX(ha) && !(ha->fw_attributes & BIT_6))
		ha->mqenable = 0;

	if (ha->mqenable) {
		bool startit = false;

		if (QLA_TGT_MODE_ENABLED())
			startit = false;

		if (ql2x_ini_mode == QLA2XXX_INI_MODE_ENABLED)
			startit = true;

		/* Create start of day qpairs for Block MQ */
		for (i = 0; i < ha->max_qpairs; i++)
			qla2xxx_create_qpair(base_vha, 5, 0, startit);
	}
	qla_init_iocb_limit(base_vha);

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
			if (ql2xprotmask)
				scsi_host_set_prot(host, ql2xprotmask);
			else
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

			if (ql2xprotguard)
				scsi_host_set_guard(host, ql2xprotguard);
			else
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
		ql_log(ql_log_info, base_vha, 0x0122,
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
	    pdev->device, ha->isp_ops->pci_info_str(base_vha, pci_info,
						       sizeof(pci_info)),
	    pci_name(pdev), ha->flags.enable_64bit_addressing ? '+' : '-',
	    base_vha->host_no,
	    ha->isp_ops->fw_version_str(base_vha, fw_str, sizeof(fw_str)));

	qlt_add_target(ha, base_vha);

	clear_bit(PFLG_DRIVER_PROBING, &base_vha->pci_flags);

	if (test_bit(UNLOADING, &base_vha->dpc_flags))
		return -ENODEV;

	return 0;

probe_failed:
	qla_enode_stop(base_vha);
	qla_edb_stop(base_vha);
	vfree(base_vha->scan.l);
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

static void __qla_set_remove_flag(scsi_qla_host_t *base_vha)
{
	scsi_qla_host_t *vp;
	unsigned long flags;
	struct qla_hw_data *ha;

	if (!base_vha)
		return;

	ha = base_vha->hw;

	spin_lock_irqsave(&ha->vport_slock, flags);
	list_for_each_entry(vp, &ha->vp_list, list)
		set_bit(PFLG_DRIVER_REMOVING, &vp->pci_flags);

	/*
	 * Indicate device removal to prevent future board_disable
	 * and wait until any pending board_disable has completed.
	 */
	set_bit(PFLG_DRIVER_REMOVING, &base_vha->pci_flags);
	spin_unlock_irqrestore(&ha->vport_slock, flags);
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
	__qla_set_remove_flag(vha);
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

	if (IS_QLA25XX(ha) ||  IS_QLA2031(ha) || IS_QLA27XX(ha) ||
	    IS_QLA28XX(ha)) {
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

		if (ha->msixbase)
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
	__qla_set_remove_flag(base_vha);
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

	/*
	 * if UNLOADING flag is already set, then continue unload,
	 * where it was set first.
	 */
	if (test_and_set_bit(UNLOADING, &base_vha->dpc_flags))
		return;

	if (IS_QLA25XX(ha) || IS_QLA2031(ha) || IS_QLA27XX(ha) ||
	    IS_QLA28XX(ha)) {
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

	qla_nvme_delete(base_vha);

	dma_free_coherent(&ha->pdev->dev,
		base_vha->gnl.size, base_vha->gnl.l, base_vha->gnl.ldma);

	base_vha->gnl.l = NULL;
	qla_enode_stop(base_vha);
	qla_edb_stop(base_vha);

	vfree(base_vha->scan.l);

	if (IS_QLAFX00(ha))
		qlafx00_driver_shutdown(base_vha, 20);

	qla2x00_delete_all_vps(ha, base_vha);

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

static inline void
qla24xx_free_purex_list(struct purex_list *list)
{
	struct purex_item *item, *next;
	ulong flags;

	spin_lock_irqsave(&list->lock, flags);
	list_for_each_entry_safe(item, next, &list->head, list) {
		list_del(&item->list);
		if (item == &item->vha->default_item)
			continue;
		kfree(item);
	}
	spin_unlock_irqrestore(&list->lock, flags);
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
		destroy_workqueue(ha->wq);
		ha->wq = NULL;
	}


	qla24xx_free_purex_list(&vha->purex_list);

	qla2x00_mem_free(ha);

	qla82xx_md_free(vha);

	qla_edif_sadb_release_free_pool(ha);
	qla_edif_sadb_release(ha);

	qla2x00_free_queues(ha);
}

void qla2x00_free_fcports(struct scsi_qla_host *vha)
{
	fc_port_t *fcport, *tfcport;

	list_for_each_entry_safe(fcport, tfcport, &vha->vp_fcports, list)
		qla2x00_free_fcport(fcport);
}

static inline void
qla2x00_schedule_rport_del(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	int now;

	if (!fcport->rport)
		return;

	if (fcport->rport) {
		ql_dbg(ql_dbg_disc, fcport->vha, 0x2109,
		    "%s %8phN. rport %p roles %x\n",
		    __func__, fcport->port_name, fcport->rport,
		    fcport->rport->roles);
		fc_remote_port_delete(fcport->rport);
	}
	qlt_do_generation_tick(vha, &now);
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
    int do_login)
{
	if (IS_QLAFX00(vha->hw)) {
		qla2x00_set_fcport_state(fcport, FCS_DEVICE_LOST);
		qla2x00_schedule_rport_del(vha, fcport);
		return;
	}

	if (atomic_read(&fcport->state) == FCS_ONLINE &&
	    vha->vp_idx == fcport->vha->vp_idx) {
		qla2x00_set_fcport_state(fcport, FCS_DEVICE_LOST);
		qla2x00_schedule_rport_del(vha, fcport);
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

void
qla2x00_mark_all_devices_lost(scsi_qla_host_t *vha)
{
	fc_port_t *fcport;

	ql_dbg(ql_dbg_disc, vha, 0x20f1,
	    "Mark all dev lost\n");

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (ql2xfc2target &&
		    fcport->loop_id != FC_NO_LOOP_ID &&
		    (fcport->flags & FCF_FCP2_DEVICE) &&
		    fcport->port_type == FCT_TARGET &&
		    !qla2x00_reset_active(vha)) {
			ql_dbg(ql_dbg_disc, vha, 0x211a,
			       "Delaying session delete for FCP2 flags 0x%x port_type = 0x%x port_id=%06x %phC",
			       fcport->flags, fcport->port_type,
			       fcport->d_id.b24, fcport->port_name);
			continue;
		}
		fcport->scan_state = 0;
		qlt_schedule_sess_for_deletion(fcport);
	}
}

static void qla2x00_set_reserved_loop_ids(struct qla_hw_data *ha)
{
	int i;

	if (IS_FWI2_CAPABLE(ha))
		return;

	for (i = 0; i < SNS_FIRST_LOOP_ID; i++)
		set_bit(i, ha->loop_id_map);
	set_bit(MANAGEMENT_SERVER, ha->loop_id_map);
	set_bit(BROADCAST, ha->loop_id_map);
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
	int rc;

	ha->init_cb = dma_alloc_coherent(&ha->pdev->dev, ha->init_cb_size,
		&ha->init_cb_dma, GFP_KERNEL);
	if (!ha->init_cb)
		goto fail;

	rc = btree_init32(&ha->host_map);
	if (rc)
		goto fail_free_init_cb;

	if (qlt_mem_alloc(ha) < 0)
		goto fail_free_btree;

	ha->gid_list = dma_alloc_coherent(&ha->pdev->dev,
		qla2x00_gid_list_size(ha), &ha->gid_list_dma, GFP_KERNEL);
	if (!ha->gid_list)
		goto fail_free_tgt_mem;

	ha->srb_mempool = mempool_create_slab_pool(SRB_MIN_REQ, srb_cachep);
	if (!ha->srb_mempool)
		goto fail_free_gid_list;

	if (IS_P3P_TYPE(ha) || IS_QLA27XX(ha) || (ql2xsecenable && IS_QLA28XX(ha))) {
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

	if (IS_P3P_TYPE(ha) || ql2xenabledif || (IS_QLA28XX(ha) && ql2xsecenable)) {
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

		if (ql2xenabledif) {
			u64 bufsize = DIF_BUNDLING_DMA_POOL_SIZE;
			struct dsd_dma *dsd, *nxt;
			uint i;
			/* Creata a DMA pool of buffers for DIF bundling */
			ha->dif_bundl_pool = dma_pool_create(name,
			    &ha->pdev->dev, DIF_BUNDLING_DMA_POOL_SIZE, 8, 0);
			if (!ha->dif_bundl_pool) {
				ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0024,
				    "%s: failed create dif_bundl_pool\n",
				    __func__);
				goto fail_dif_bundl_dma_pool;
			}

			INIT_LIST_HEAD(&ha->pool.good.head);
			INIT_LIST_HEAD(&ha->pool.unusable.head);
			ha->pool.good.count = 0;
			ha->pool.unusable.count = 0;
			for (i = 0; i < 128; i++) {
				dsd = kzalloc(sizeof(*dsd), GFP_ATOMIC);
				if (!dsd) {
					ql_dbg_pci(ql_dbg_init, ha->pdev,
					    0xe0ee, "%s: failed alloc dsd\n",
					    __func__);
					return -ENOMEM;
				}
				ha->dif_bundle_kallocs++;

				dsd->dsd_addr = dma_pool_alloc(
				    ha->dif_bundl_pool, GFP_ATOMIC,
				    &dsd->dsd_list_dma);
				if (!dsd->dsd_addr) {
					ql_dbg_pci(ql_dbg_init, ha->pdev,
					    0xe0ee,
					    "%s: failed alloc ->dsd_addr\n",
					    __func__);
					kfree(dsd);
					ha->dif_bundle_kallocs--;
					continue;
				}
				ha->dif_bundle_dma_allocs++;

				/*
				 * if DMA buffer crosses 4G boundary,
				 * put it on bad list
				 */
				if (MSD(dsd->dsd_list_dma) ^
				    MSD(dsd->dsd_list_dma + bufsize)) {
					list_add_tail(&dsd->list,
					    &ha->pool.unusable.head);
					ha->pool.unusable.count++;
				} else {
					list_add_tail(&dsd->list,
					    &ha->pool.good.head);
					ha->pool.good.count++;
				}
			}

			/* return the good ones back to the pool */
			list_for_each_entry_safe(dsd, nxt,
			    &ha->pool.good.head, list) {
				list_del(&dsd->list);
				dma_pool_free(ha->dif_bundl_pool,
				    dsd->dsd_addr, dsd->dsd_list_dma);
				ha->dif_bundle_dma_allocs--;
				kfree(dsd);
				ha->dif_bundle_kallocs--;
			}

			ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0024,
			    "%s: dif dma pool (good=%u unusable=%u)\n",
			    __func__, ha->pool.good.count,
			    ha->pool.unusable.count);
		}

		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0025,
		    "dl_dma_pool=%p fcp_cmnd_dma_pool=%p dif_bundl_pool=%p.\n",
		    ha->dl_dma_pool, ha->fcp_cmnd_dma_pool,
		    ha->dif_bundl_pool);
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
	if (IS_CNA_CAPABLE(ha) || IS_QLA2031(ha) || IS_QLA27XX(ha) ||
	    IS_QLA28XX(ha)) {
		ha->ex_init_cb = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
		    &ha->ex_init_cb_dma);
		if (!ha->ex_init_cb)
			goto fail_ex_init_cb;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x002e,
		    "ex_init_cb=%p.\n", ha->ex_init_cb);
	}

	/* Get consistent memory allocated for Special Features-CB. */
	if (IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
		ha->sf_init_cb = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL,
						&ha->sf_init_cb_dma);
		if (!ha->sf_init_cb)
			goto fail_sf_init_cb;
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0199,
			   "sf_init_cb=%p.\n", ha->sf_init_cb);
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

	ha->flt = dma_alloc_coherent(&ha->pdev->dev,
	    sizeof(struct qla_flt_header) + FLT_REGIONS_SIZE, &ha->flt_dma,
	    GFP_KERNEL);
	if (!ha->flt) {
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x011b,
		    "Unable to allocate memory for FLT.\n");
		goto fail_flt_buffer;
	}

	/* allocate the purex dma pool */
	ha->purex_dma_pool = dma_pool_create(name, &ha->pdev->dev,
	    ELS_MAX_PAYLOAD, 8, 0);

	if (!ha->purex_dma_pool) {
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0x011b,
		    "Unable to allocate purex_dma_pool.\n");
		goto fail_flt;
	}

	ha->elsrej.size = sizeof(struct fc_els_ls_rjt) + 16;
	ha->elsrej.c = dma_alloc_coherent(&ha->pdev->dev,
	    ha->elsrej.size, &ha->elsrej.cdma, GFP_KERNEL);

	if (!ha->elsrej.c) {
		ql_dbg_pci(ql_dbg_init, ha->pdev, 0xffff,
		    "Alloc failed for els reject cmd.\n");
		goto fail_elsrej;
	}
	ha->elsrej.c->er_cmd = ELS_LS_RJT;
	ha->elsrej.c->er_reason = ELS_RJT_LOGIC;
	ha->elsrej.c->er_explan = ELS_EXPL_UNAB_DATA;
	return 0;

fail_elsrej:
	dma_pool_destroy(ha->purex_dma_pool);
fail_flt:
	dma_free_coherent(&ha->pdev->dev, SFP_DEV_SIZE,
	    ha->flt, ha->flt_dma);

fail_flt_buffer:
	dma_free_coherent(&ha->pdev->dev, SFP_DEV_SIZE,
	    ha->sfp_data, ha->sfp_data_dma);
fail_sfp_data:
	kfree(ha->loop_id_map);
fail_loop_id_map:
	dma_pool_free(ha->s_dma_pool, ha->async_pd, ha->async_pd_dma);
fail_async_pd:
	dma_pool_free(ha->s_dma_pool, ha->sf_init_cb, ha->sf_init_cb_dma);
fail_sf_init_cb:
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
	if (ql2xenabledif) {
		struct dsd_dma *dsd, *nxt;

		list_for_each_entry_safe(dsd, nxt, &ha->pool.unusable.head,
		    list) {
			list_del(&dsd->list);
			dma_pool_free(ha->dif_bundl_pool, dsd->dsd_addr,
			    dsd->dsd_list_dma);
			ha->dif_bundle_dma_allocs--;
			kfree(dsd);
			ha->dif_bundle_kallocs--;
			ha->pool.unusable.count--;
		}
		dma_pool_destroy(ha->dif_bundl_pool);
		ha->dif_bundl_pool = NULL;
	}

fail_dif_bundl_dma_pool:
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
	mempool_destroy(ha->ctx_mempool);
	ha->ctx_mempool = NULL;
fail_free_srb_mempool:
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
fail_free_btree:
	btree_destroy32(&ha->host_map);
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
	uint16_t	size, max_cnt;
	uint32_t temp;
	struct qla_hw_data *ha = vha->hw;

	/* Return if we don't need to alloacate any extended logins */
	if (ql2xexlogins <= MAX_FIBRE_DEVICES_2400)
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
	struct init_cb_81xx *icb = (struct init_cb_81xx *)&vha->hw->init_cb;
	*ret_cnt = FW_DEF_EXCHANGES_CNT;

	if (max_cnt > vha->hw->max_exchg)
		max_cnt = vha->hw->max_exchg;

	if (qla_ini_mode_enabled(vha)) {
		if (vha->ql2xiniexchg > max_cnt)
			vha->ql2xiniexchg = max_cnt;

		if (vha->ql2xiniexchg > FW_DEF_EXCHANGES_CNT)
			*ret_cnt = vha->ql2xiniexchg;

	} else if (qla_tgt_mode_enabled(vha)) {
		if (vha->ql2xexchoffld > max_cnt) {
			vha->ql2xexchoffld = max_cnt;
			icb->exchange_count = cpu_to_le16(vha->ql2xexchoffld);
		}

		if (vha->ql2xexchoffld > FW_DEF_EXCHANGES_CNT)
			*ret_cnt = vha->ql2xexchoffld;
	} else if (qla_dual_mode_enabled(vha)) {
		temp = vha->ql2xiniexchg + vha->ql2xexchoffld;
		if (temp > max_cnt) {
			vha->ql2xiniexchg -= (temp - max_cnt)/2;
			vha->ql2xexchoffld -= (((temp - max_cnt)/2) + 1);
			temp = max_cnt;
			icb->exchange_count = cpu_to_le16(vha->ql2xexchoffld);
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
		if (actual_cnt <= FW_DEF_EXCHANGES_CNT) {
			ha->exchoffld_size = 0;
			ha->flags.exchoffld_enabled = 0;
			return QLA_SUCCESS;
		}

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
	} else if (!ha->exchoffld_buf || (actual_cnt <= FW_DEF_EXCHANGES_CNT)) {
		/* pathological case */
		qla2x00_free_exchoffld_buffer(ha);
		ha->exchoffld_size = 0;
		ha->flags.exchoffld_enabled = 0;
		ql_log(ql_log_info, vha, 0xd016,
		    "Exchange offload not enable: offld size=%d, actual count=%d entry sz=0x%x, total sz=0x%x.\n",
		    ha->exchoffld_size, actual_cnt, size, totsz);
		return 0;
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
			icb->exchange_count = cpu_to_le16(vha->ql2xexchoffld);
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
	struct fwdt *fwdt = ha->fwdt;
	uint j;

	if (ha->fce)
		dma_free_coherent(&ha->pdev->dev,
		    FCE_SIZE, ha->fce, ha->fce_dma);

	if (ha->eft)
		dma_free_coherent(&ha->pdev->dev,
		    EFT_SIZE, ha->eft, ha->eft_dma);

	vfree(ha->fw_dump);

	ha->fce = NULL;
	ha->fce_dma = 0;
	ha->flags.fce_enabled = 0;
	ha->eft = NULL;
	ha->eft_dma = 0;
	ha->fw_dumped = false;
	ha->fw_dump_cap_flags = 0;
	ha->fw_dump_reading = 0;
	ha->fw_dump = NULL;
	ha->fw_dump_len = 0;

	for (j = 0; j < 2; j++, fwdt++) {
		vfree(fwdt->template);
		fwdt->template = NULL;
		fwdt->length = 0;
	}
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
	ha->mctp_dump = NULL;

	mempool_destroy(ha->srb_mempool);
	ha->srb_mempool = NULL;

	if (ha->dcbx_tlv)
		dma_free_coherent(&ha->pdev->dev, DCBX_TLV_DATA_SIZE,
		    ha->dcbx_tlv, ha->dcbx_tlv_dma);
	ha->dcbx_tlv = NULL;

	if (ha->xgmac_data)
		dma_free_coherent(&ha->pdev->dev, XGMAC_DATA_SIZE,
		    ha->xgmac_data, ha->xgmac_data_dma);
	ha->xgmac_data = NULL;

	if (ha->sns_cmd)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct sns_cmd_pkt),
		ha->sns_cmd, ha->sns_cmd_dma);
	ha->sns_cmd = NULL;
	ha->sns_cmd_dma = 0;

	if (ha->ct_sns)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct ct_sns_pkt),
		ha->ct_sns, ha->ct_sns_dma);
	ha->ct_sns = NULL;
	ha->ct_sns_dma = 0;

	if (ha->sfp_data)
		dma_free_coherent(&ha->pdev->dev, SFP_DEV_SIZE, ha->sfp_data,
		    ha->sfp_data_dma);
	ha->sfp_data = NULL;

	if (ha->flt)
		dma_free_coherent(&ha->pdev->dev,
		    sizeof(struct qla_flt_header) + FLT_REGIONS_SIZE,
		    ha->flt, ha->flt_dma);
	ha->flt = NULL;
	ha->flt_dma = 0;

	if (ha->ms_iocb)
		dma_pool_free(ha->s_dma_pool, ha->ms_iocb, ha->ms_iocb_dma);
	ha->ms_iocb = NULL;
	ha->ms_iocb_dma = 0;

	if (ha->sf_init_cb)
		dma_pool_free(ha->s_dma_pool,
			      ha->sf_init_cb, ha->sf_init_cb_dma);

	if (ha->ex_init_cb)
		dma_pool_free(ha->s_dma_pool,
			ha->ex_init_cb, ha->ex_init_cb_dma);
	ha->ex_init_cb = NULL;
	ha->ex_init_cb_dma = 0;

	if (ha->async_pd)
		dma_pool_free(ha->s_dma_pool, ha->async_pd, ha->async_pd_dma);
	ha->async_pd = NULL;
	ha->async_pd_dma = 0;

	dma_pool_destroy(ha->s_dma_pool);
	ha->s_dma_pool = NULL;

	if (ha->gid_list)
		dma_free_coherent(&ha->pdev->dev, qla2x00_gid_list_size(ha),
		ha->gid_list, ha->gid_list_dma);
	ha->gid_list = NULL;
	ha->gid_list_dma = 0;

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

	dma_pool_destroy(ha->dl_dma_pool);
	ha->dl_dma_pool = NULL;

	dma_pool_destroy(ha->fcp_cmnd_dma_pool);
	ha->fcp_cmnd_dma_pool = NULL;

	mempool_destroy(ha->ctx_mempool);
	ha->ctx_mempool = NULL;

	if (ql2xenabledif && ha->dif_bundl_pool) {
		struct dsd_dma *dsd, *nxt;

		list_for_each_entry_safe(dsd, nxt, &ha->pool.unusable.head,
					 list) {
			list_del(&dsd->list);
			dma_pool_free(ha->dif_bundl_pool, dsd->dsd_addr,
				      dsd->dsd_list_dma);
			ha->dif_bundle_dma_allocs--;
			kfree(dsd);
			ha->dif_bundle_kallocs--;
			ha->pool.unusable.count--;
		}
		list_for_each_entry_safe(dsd, nxt, &ha->pool.good.head, list) {
			list_del(&dsd->list);
			dma_pool_free(ha->dif_bundl_pool, dsd->dsd_addr,
				      dsd->dsd_list_dma);
			ha->dif_bundle_dma_allocs--;
			kfree(dsd);
			ha->dif_bundle_kallocs--;
		}
	}

	dma_pool_destroy(ha->dif_bundl_pool);
	ha->dif_bundl_pool = NULL;

	qlt_mem_free(ha);
	qla_remove_hostmap(ha);

	if (ha->init_cb)
		dma_free_coherent(&ha->pdev->dev, ha->init_cb_size,
			ha->init_cb, ha->init_cb_dma);

	dma_pool_destroy(ha->purex_dma_pool);
	ha->purex_dma_pool = NULL;

	if (ha->elsrej.c) {
		dma_free_coherent(&ha->pdev->dev, ha->elsrej.size,
		    ha->elsrej.c, ha->elsrej.cdma);
		ha->elsrej.c = NULL;
	}

	ha->init_cb = NULL;
	ha->init_cb_dma = 0;

	vfree(ha->optrom_buffer);
	ha->optrom_buffer = NULL;
	kfree(ha->nvram);
	ha->nvram = NULL;
	kfree(ha->npiv_info);
	ha->npiv_info = NULL;
	kfree(ha->swl);
	ha->swl = NULL;
	kfree(ha->loop_id_map);
	ha->sf_init_cb = NULL;
	ha->sf_init_cb_dma = 0;
	ha->loop_id_map = NULL;
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

	vha->qlini_mode = ql2x_ini_mode;
	vha->ql2xexchoffld = ql2xexchoffld;
	vha->ql2xiniexchg = ql2xiniexchg;

	INIT_LIST_HEAD(&vha->vp_fcports);
	INIT_LIST_HEAD(&vha->work_list);
	INIT_LIST_HEAD(&vha->list);
	INIT_LIST_HEAD(&vha->qla_cmd_list);
	INIT_LIST_HEAD(&vha->logo_list);
	INIT_LIST_HEAD(&vha->plogi_ack_list);
	INIT_LIST_HEAD(&vha->qp_list);
	INIT_LIST_HEAD(&vha->gnl.fcports);
	INIT_LIST_HEAD(&vha->gpnid_list);
	INIT_WORK(&vha->iocb_work, qla2x00_iocb_work_fn);

	INIT_LIST_HEAD(&vha->purex_list.head);
	spin_lock_init(&vha->purex_list.lock);

	spin_lock_init(&vha->work_lock);
	spin_lock_init(&vha->cmd_list_lock);
	init_waitqueue_head(&vha->fcport_waitQ);
	init_waitqueue_head(&vha->vref_waitq);
	qla_enode_init(vha);
	qla_edb_init(vha);


	vha->gnl.size = sizeof(struct get_name_list_extended) *
			(ha->max_loop_id + 1);
	vha->gnl.l = dma_alloc_coherent(&ha->pdev->dev,
	    vha->gnl.size, &vha->gnl.ldma, GFP_KERNEL);
	if (!vha->gnl.l) {
		ql_log(ql_log_fatal, vha, 0xd04a,
		    "Alloc failed for name list.\n");
		scsi_host_put(vha->host);
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
		scsi_host_put(vha->host);
		return NULL;
	}
	INIT_DELAYED_WORK(&vha->scan.scan_work, qla_scan_work_fn);

	sprintf(vha->host_str, "%s_%lu", QLA2XXX_DRIVER_NAME, vha->host_no);
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

	if (test_bit(UNLOADING, &vha->dpc_flags))
		return NULL;

	if (qla_vha_mark_busy(vha))
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
qla2x00_post_async_work(adisc, QLA_EVT_ASYNC_ADISC);
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
		snprintf(event_string, sizeof(event_string), "FW_DUMP=%lu",
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

void qla24xx_sched_upd_fcport(fc_port_t *fcport)
{
	unsigned long flags;

	if (IS_SW_RESV_ADDR(fcport->d_id))
		return;

	spin_lock_irqsave(&fcport->vha->work_lock, flags);
	if (fcport->disc_state == DSC_UPD_FCPORT) {
		spin_unlock_irqrestore(&fcport->vha->work_lock, flags);
		return;
	}
	fcport->jiffies_at_registration = jiffies;
	fcport->sec_since_registration = 0;
	fcport->next_disc_state = DSC_DELETED;
	qla2x00_set_fcport_disc_state(fcport, DSC_UPD_FCPORT);
	spin_unlock_irqrestore(&fcport->vha->work_lock, flags);

	queue_work(system_unbound_wq, &fcport->reg_work);
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
			fcport->tgt_short_link_down_cnt = 0;

			memcpy(fcport->port_name, e->u.new_sess.port_name,
			    WWN_SIZE);

			fcport->fc4_type = e->u.new_sess.fc4_type;
			if (NVME_PRIORITY(vha->hw, fcport))
				fcport->do_prli_nvme = 1;
			else
				fcport->do_prli_nvme = 0;

			if (e->u.new_sess.fc4_type & FS_FCP_IS_N2N) {
				fcport->dm_login_expire = jiffies +
					QLA_N2N_WAIT_TIME * HZ;
				fcport->fc4_type = FS_FC4TYPE_FCP;
				fcport->n2n_flag = 1;
				if (vha->flags.nvme_enabled)
					fcport->fc4_type |= FS_FC4TYPE_NVME;
			}

		} else {
			ql_dbg(ql_dbg_disc, vha, 0xffff,
				   "%s %8phC mem alloc fail.\n",
				   __func__, e->u.new_sess.port_name);

			if (pla) {
				list_del(&pla->list);
				kmem_cache_free(qla_tgt_plogi_cachep, pla);
			}
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

			if (N2N_TOPO(vha->hw)) {
				fcport->flags &= ~FCF_FABRIC_DEVICE;
				fcport->keep_nport_handle = 1;
				if (vha->flags.nvme_enabled) {
					fcport->fc4_type =
					    (FS_FC4TYPE_NVME | FS_FC4TYPE_FCP);
					fcport->n2n_flag = 1;
				}
				fcport->fw_login_state = 0;

				schedule_delayed_work(&vha->scan.scan_work, 5);
			} else {
				qla24xx_fcport_handle_login(vha, fcport);
			}
		}
	}

	if (free_fcport) {
		qla2x00_free_fcport(fcport);
		if (pla) {
			list_del(&pla->list);
			kmem_cache_free(qla_tgt_plogi_cachep, pla);
		}
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
	int rc;

	spin_lock_irqsave(&vha->work_lock, flags);
	list_splice_init(&vha->work_list, &work);
	spin_unlock_irqrestore(&vha->work_lock, flags);

	list_for_each_entry_safe(e, tmp, &work, list) {
		rc = QLA_SUCCESS;
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
			rc = qla2x00_async_logout(vha, e->u.logio.fcport);
			break;
		case QLA_EVT_ASYNC_ADISC:
			qla2x00_async_adisc(vha, e->u.logio.fcport,
			    e->u.logio.data);
			break;
		case QLA_EVT_UEVENT:
			qla2x00_uevent_emit(vha, e->u.uevent.code);
			break;
		case QLA_EVT_AENFX:
			qlafx00_process_aen(vha, e);
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
		case QLA_EVT_GNL:
			qla24xx_async_gnl(vha, e->u.fcport.fcport);
			break;
		case QLA_EVT_NACK:
			qla24xx_do_nack_work(vha, e);
			break;
		case QLA_EVT_ASYNC_PRLO:
			rc = qla2x00_async_prlo(vha, e->u.logio.fcport);
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
		case QLA_EVT_SA_REPLACE:
			rc = qla24xx_issue_sa_replace_iocb(vha, e);
			break;
		}

		if (rc == EAGAIN) {
			/* put 'work' at head of 'vha->work_list' */
			spin_lock_irqsave(&vha->work_lock, flags);
			list_splice(&work, &vha->work_list);
			spin_unlock_irqrestore(&vha->work_lock, flags);
			break;
		}
		list_del_init(&e->list);
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
			    fcport->disc_state == DSC_LOGIN_AUTH_PEND ||
			    fcport->disc_state == DSC_LOGIN_COMPLETE)
				continue;

			if (fcport->flags & (FCF_ASYNC_SENT|FCF_ASYNC_ACTIVE) ||
				fcport->disc_state == DSC_DELETE_PEND) {
				relogin_needed = 1;
			} else {
				if (vha->hw->current_topology != ISP_CFG_NL) {
					memset(&ea, 0, sizeof(ea));
					ea.fcport = fcport;
					qla24xx_handle_relogin_event(vha, &ea);
				} else if (vha->hw->current_topology ==
					 ISP_CFG_NL &&
					IS_QLA2XXX_MIDTYPE(vha->hw)) {
					(void)qla24xx_fcport_handle_login(vha,
									fcport);
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

/*
 * Control the frequency of IDC lock retries
 */
#define QLA83XX_WAIT_LOGIC_MS	100

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
		msleep(QLA83XX_WAIT_LOGIC_MS);
		goto retry_lockid;
	} else
		return QLA_SUCCESS;

exit:
	return rval;
}

/*
 * Context: task, can sleep
 */
void
qla83xx_idc_lock(scsi_qla_host_t *base_vha, uint16_t requester_id)
{
	uint32_t data;
	uint32_t lock_owner;
	struct qla_hw_data *ha = base_vha->hw;

	might_sleep();

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
				msleep(QLA83XX_WAIT_LOGIC_MS);
				goto retry_lock;
			} else
				ql_log(ql_log_warn, base_vha, 0xb075,
				    "IDC Lock recovery FAILED.\n");
		}

	}

	return;
}

static bool
qla25xx_rdp_rsp_reduce_size(struct scsi_qla_host *vha,
	struct purex_entry_24xx *purex)
{
	char fwstr[16];
	u32 sid = purex->s_id[2] << 16 | purex->s_id[1] << 8 | purex->s_id[0];
	struct port_database_24xx *pdb;

	/* Domain Controller is always logged-out. */
	/* if RDP request is not from Domain Controller: */
	if (sid != 0xfffc01)
		return false;

	ql_dbg(ql_dbg_init, vha, 0x0181, "%s: s_id=%#x\n", __func__, sid);

	pdb = kzalloc(sizeof(*pdb), GFP_KERNEL);
	if (!pdb) {
		ql_dbg(ql_dbg_init, vha, 0x0181,
		    "%s: Failed allocate pdb\n", __func__);
	} else if (qla24xx_get_port_database(vha,
				le16_to_cpu(purex->nport_handle), pdb)) {
		ql_dbg(ql_dbg_init, vha, 0x0181,
		    "%s: Failed get pdb sid=%x\n", __func__, sid);
	} else if (pdb->current_login_state != PDS_PLOGI_COMPLETE &&
	    pdb->current_login_state != PDS_PRLI_COMPLETE) {
		ql_dbg(ql_dbg_init, vha, 0x0181,
		    "%s: Port not logged in sid=%#x\n", __func__, sid);
	} else {
		/* RDP request is from logged in port */
		kfree(pdb);
		return false;
	}
	kfree(pdb);

	vha->hw->isp_ops->fw_version_str(vha, fwstr, sizeof(fwstr));
	fwstr[strcspn(fwstr, " ")] = 0;
	/* if FW version allows RDP response length upto 2048 bytes: */
	if (strcmp(fwstr, "8.09.00") > 0 || strcmp(fwstr, "8.05.65") == 0)
		return false;

	ql_dbg(ql_dbg_init, vha, 0x0181, "%s: fw=%s\n", __func__, fwstr);

	/* RDP response length is to be reduced to maximum 256 bytes */
	return true;
}

/*
 * Function Name: qla24xx_process_purex_iocb
 *
 * Description:
 * Prepare a RDP response and send to Fabric switch
 *
 * PARAMETERS:
 * vha:	SCSI qla host
 * purex: RDP request received by HBA
 */
void qla24xx_process_purex_rdp(struct scsi_qla_host *vha,
			       struct purex_item *item)
{
	struct qla_hw_data *ha = vha->hw;
	struct purex_entry_24xx *purex =
	    (struct purex_entry_24xx *)&item->iocb;
	dma_addr_t rsp_els_dma;
	dma_addr_t rsp_payload_dma;
	dma_addr_t stat_dma;
	dma_addr_t sfp_dma;
	struct els_entry_24xx *rsp_els = NULL;
	struct rdp_rsp_payload *rsp_payload = NULL;
	struct link_statistics *stat = NULL;
	uint8_t *sfp = NULL;
	uint16_t sfp_flags = 0;
	uint rsp_payload_length = sizeof(*rsp_payload);
	int rval;

	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x0180,
	    "%s: Enter\n", __func__);

	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x0181,
	    "-------- ELS REQ -------\n");
	ql_dump_buffer(ql_dbg_init + ql_dbg_verbose, vha, 0x0182,
	    purex, sizeof(*purex));

	if (qla25xx_rdp_rsp_reduce_size(vha, purex)) {
		rsp_payload_length =
		    offsetof(typeof(*rsp_payload), optical_elmt_desc);
		ql_dbg(ql_dbg_init, vha, 0x0181,
		    "Reducing RSP payload length to %u bytes...\n",
		    rsp_payload_length);
	}

	rsp_els = dma_alloc_coherent(&ha->pdev->dev, sizeof(*rsp_els),
	    &rsp_els_dma, GFP_KERNEL);
	if (!rsp_els) {
		ql_log(ql_log_warn, vha, 0x0183,
		    "Failed allocate dma buffer ELS RSP.\n");
		goto dealloc;
	}

	rsp_payload = dma_alloc_coherent(&ha->pdev->dev, sizeof(*rsp_payload),
	    &rsp_payload_dma, GFP_KERNEL);
	if (!rsp_payload) {
		ql_log(ql_log_warn, vha, 0x0184,
		    "Failed allocate dma buffer ELS RSP payload.\n");
		goto dealloc;
	}

	sfp = dma_alloc_coherent(&ha->pdev->dev, SFP_RTDI_LEN,
	    &sfp_dma, GFP_KERNEL);

	stat = dma_alloc_coherent(&ha->pdev->dev, sizeof(*stat),
	    &stat_dma, GFP_KERNEL);

	/* Prepare Response IOCB */
	rsp_els->entry_type = ELS_IOCB_TYPE;
	rsp_els->entry_count = 1;
	rsp_els->sys_define = 0;
	rsp_els->entry_status = 0;
	rsp_els->handle = 0;
	rsp_els->nport_handle = purex->nport_handle;
	rsp_els->tx_dsd_count = cpu_to_le16(1);
	rsp_els->vp_index = purex->vp_idx;
	rsp_els->sof_type = EST_SOFI3;
	rsp_els->rx_xchg_address = purex->rx_xchg_addr;
	rsp_els->rx_dsd_count = 0;
	rsp_els->opcode = purex->els_frame_payload[0];

	rsp_els->d_id[0] = purex->s_id[0];
	rsp_els->d_id[1] = purex->s_id[1];
	rsp_els->d_id[2] = purex->s_id[2];

	rsp_els->control_flags = cpu_to_le16(EPD_ELS_ACC);
	rsp_els->rx_byte_count = 0;
	rsp_els->tx_byte_count = cpu_to_le32(rsp_payload_length);

	put_unaligned_le64(rsp_payload_dma, &rsp_els->tx_address);
	rsp_els->tx_len = rsp_els->tx_byte_count;

	rsp_els->rx_address = 0;
	rsp_els->rx_len = 0;

	/* Prepare Response Payload */
	rsp_payload->hdr.cmd = cpu_to_be32(0x2 << 24); /* LS_ACC */
	rsp_payload->hdr.len = cpu_to_be32(le32_to_cpu(rsp_els->tx_byte_count) -
					   sizeof(rsp_payload->hdr));

	/* Link service Request Info Descriptor */
	rsp_payload->ls_req_info_desc.desc_tag = cpu_to_be32(0x1);
	rsp_payload->ls_req_info_desc.desc_len =
	    cpu_to_be32(RDP_DESC_LEN(rsp_payload->ls_req_info_desc));
	rsp_payload->ls_req_info_desc.req_payload_word_0 =
	    cpu_to_be32p((uint32_t *)purex->els_frame_payload);

	/* Link service Request Info Descriptor 2 */
	rsp_payload->ls_req_info_desc2.desc_tag = cpu_to_be32(0x1);
	rsp_payload->ls_req_info_desc2.desc_len =
	    cpu_to_be32(RDP_DESC_LEN(rsp_payload->ls_req_info_desc2));
	rsp_payload->ls_req_info_desc2.req_payload_word_0 =
	    cpu_to_be32p((uint32_t *)purex->els_frame_payload);


	rsp_payload->sfp_diag_desc.desc_tag = cpu_to_be32(0x10000);
	rsp_payload->sfp_diag_desc.desc_len =
		cpu_to_be32(RDP_DESC_LEN(rsp_payload->sfp_diag_desc));

	if (sfp) {
		/* SFP Flags */
		memset(sfp, 0, SFP_RTDI_LEN);
		rval = qla2x00_read_sfp(vha, sfp_dma, sfp, 0xa0, 0x7, 2, 0);
		if (!rval) {
			/* SFP Flags bits 3-0: Port Tx Laser Type */
			if (sfp[0] & BIT_2 || sfp[1] & (BIT_6|BIT_5))
				sfp_flags |= BIT_0; /* short wave */
			else if (sfp[0] & BIT_1)
				sfp_flags |= BIT_1; /* long wave 1310nm */
			else if (sfp[1] & BIT_4)
				sfp_flags |= BIT_1|BIT_0; /* long wave 1550nm */
		}

		/* SFP Type */
		memset(sfp, 0, SFP_RTDI_LEN);
		rval = qla2x00_read_sfp(vha, sfp_dma, sfp, 0xa0, 0x0, 1, 0);
		if (!rval) {
			sfp_flags |= BIT_4; /* optical */
			if (sfp[0] == 0x3)
				sfp_flags |= BIT_6; /* sfp+ */
		}

		rsp_payload->sfp_diag_desc.sfp_flags = cpu_to_be16(sfp_flags);

		/* SFP Diagnostics */
		memset(sfp, 0, SFP_RTDI_LEN);
		rval = qla2x00_read_sfp(vha, sfp_dma, sfp, 0xa2, 0x60, 10, 0);
		if (!rval) {
			__be16 *trx = (__force __be16 *)sfp; /* already be16 */
			rsp_payload->sfp_diag_desc.temperature = trx[0];
			rsp_payload->sfp_diag_desc.vcc = trx[1];
			rsp_payload->sfp_diag_desc.tx_bias = trx[2];
			rsp_payload->sfp_diag_desc.tx_power = trx[3];
			rsp_payload->sfp_diag_desc.rx_power = trx[4];
		}
	}

	/* Port Speed Descriptor */
	rsp_payload->port_speed_desc.desc_tag = cpu_to_be32(0x10001);
	rsp_payload->port_speed_desc.desc_len =
	    cpu_to_be32(RDP_DESC_LEN(rsp_payload->port_speed_desc));
	rsp_payload->port_speed_desc.speed_capab = cpu_to_be16(
	    qla25xx_fdmi_port_speed_capability(ha));
	rsp_payload->port_speed_desc.operating_speed = cpu_to_be16(
	    qla25xx_fdmi_port_speed_currently(ha));

	/* Link Error Status Descriptor */
	rsp_payload->ls_err_desc.desc_tag = cpu_to_be32(0x10002);
	rsp_payload->ls_err_desc.desc_len =
		cpu_to_be32(RDP_DESC_LEN(rsp_payload->ls_err_desc));

	if (stat) {
		rval = qla24xx_get_isp_stats(vha, stat, stat_dma, 0);
		if (!rval) {
			rsp_payload->ls_err_desc.link_fail_cnt =
			    cpu_to_be32(le32_to_cpu(stat->link_fail_cnt));
			rsp_payload->ls_err_desc.loss_sync_cnt =
			    cpu_to_be32(le32_to_cpu(stat->loss_sync_cnt));
			rsp_payload->ls_err_desc.loss_sig_cnt =
			    cpu_to_be32(le32_to_cpu(stat->loss_sig_cnt));
			rsp_payload->ls_err_desc.prim_seq_err_cnt =
			    cpu_to_be32(le32_to_cpu(stat->prim_seq_err_cnt));
			rsp_payload->ls_err_desc.inval_xmit_word_cnt =
			    cpu_to_be32(le32_to_cpu(stat->inval_xmit_word_cnt));
			rsp_payload->ls_err_desc.inval_crc_cnt =
			    cpu_to_be32(le32_to_cpu(stat->inval_crc_cnt));
			rsp_payload->ls_err_desc.pn_port_phy_type |= BIT_6;
		}
	}

	/* Portname Descriptor */
	rsp_payload->port_name_diag_desc.desc_tag = cpu_to_be32(0x10003);
	rsp_payload->port_name_diag_desc.desc_len =
	    cpu_to_be32(RDP_DESC_LEN(rsp_payload->port_name_diag_desc));
	memcpy(rsp_payload->port_name_diag_desc.WWNN,
	    vha->node_name,
	    sizeof(rsp_payload->port_name_diag_desc.WWNN));
	memcpy(rsp_payload->port_name_diag_desc.WWPN,
	    vha->port_name,
	    sizeof(rsp_payload->port_name_diag_desc.WWPN));

	/* F-Port Portname Descriptor */
	rsp_payload->port_name_direct_desc.desc_tag = cpu_to_be32(0x10003);
	rsp_payload->port_name_direct_desc.desc_len =
	    cpu_to_be32(RDP_DESC_LEN(rsp_payload->port_name_direct_desc));
	memcpy(rsp_payload->port_name_direct_desc.WWNN,
	    vha->fabric_node_name,
	    sizeof(rsp_payload->port_name_direct_desc.WWNN));
	memcpy(rsp_payload->port_name_direct_desc.WWPN,
	    vha->fabric_port_name,
	    sizeof(rsp_payload->port_name_direct_desc.WWPN));

	/* Bufer Credit Descriptor */
	rsp_payload->buffer_credit_desc.desc_tag = cpu_to_be32(0x10006);
	rsp_payload->buffer_credit_desc.desc_len =
		cpu_to_be32(RDP_DESC_LEN(rsp_payload->buffer_credit_desc));
	rsp_payload->buffer_credit_desc.fcport_b2b = 0;
	rsp_payload->buffer_credit_desc.attached_fcport_b2b = cpu_to_be32(0);
	rsp_payload->buffer_credit_desc.fcport_rtt = cpu_to_be32(0);

	if (ha->flags.plogi_template_valid) {
		uint32_t tmp =
		be16_to_cpu(ha->plogi_els_payld.fl_csp.sp_bb_cred);
		rsp_payload->buffer_credit_desc.fcport_b2b = cpu_to_be32(tmp);
	}

	if (rsp_payload_length < sizeof(*rsp_payload))
		goto send;

	/* Optical Element Descriptor, Temperature */
	rsp_payload->optical_elmt_desc[0].desc_tag = cpu_to_be32(0x10007);
	rsp_payload->optical_elmt_desc[0].desc_len =
		cpu_to_be32(RDP_DESC_LEN(*rsp_payload->optical_elmt_desc));
	/* Optical Element Descriptor, Voltage */
	rsp_payload->optical_elmt_desc[1].desc_tag = cpu_to_be32(0x10007);
	rsp_payload->optical_elmt_desc[1].desc_len =
		cpu_to_be32(RDP_DESC_LEN(*rsp_payload->optical_elmt_desc));
	/* Optical Element Descriptor, Tx Bias Current */
	rsp_payload->optical_elmt_desc[2].desc_tag = cpu_to_be32(0x10007);
	rsp_payload->optical_elmt_desc[2].desc_len =
		cpu_to_be32(RDP_DESC_LEN(*rsp_payload->optical_elmt_desc));
	/* Optical Element Descriptor, Tx Power */
	rsp_payload->optical_elmt_desc[3].desc_tag = cpu_to_be32(0x10007);
	rsp_payload->optical_elmt_desc[3].desc_len =
		cpu_to_be32(RDP_DESC_LEN(*rsp_payload->optical_elmt_desc));
	/* Optical Element Descriptor, Rx Power */
	rsp_payload->optical_elmt_desc[4].desc_tag = cpu_to_be32(0x10007);
	rsp_payload->optical_elmt_desc[4].desc_len =
		cpu_to_be32(RDP_DESC_LEN(*rsp_payload->optical_elmt_desc));

	if (sfp) {
		memset(sfp, 0, SFP_RTDI_LEN);
		rval = qla2x00_read_sfp(vha, sfp_dma, sfp, 0xa2, 0, 64, 0);
		if (!rval) {
			__be16 *trx = (__force __be16 *)sfp; /* already be16 */

			/* Optical Element Descriptor, Temperature */
			rsp_payload->optical_elmt_desc[0].high_alarm = trx[0];
			rsp_payload->optical_elmt_desc[0].low_alarm = trx[1];
			rsp_payload->optical_elmt_desc[0].high_warn = trx[2];
			rsp_payload->optical_elmt_desc[0].low_warn = trx[3];
			rsp_payload->optical_elmt_desc[0].element_flags =
			    cpu_to_be32(1 << 28);

			/* Optical Element Descriptor, Voltage */
			rsp_payload->optical_elmt_desc[1].high_alarm = trx[4];
			rsp_payload->optical_elmt_desc[1].low_alarm = trx[5];
			rsp_payload->optical_elmt_desc[1].high_warn = trx[6];
			rsp_payload->optical_elmt_desc[1].low_warn = trx[7];
			rsp_payload->optical_elmt_desc[1].element_flags =
			    cpu_to_be32(2 << 28);

			/* Optical Element Descriptor, Tx Bias Current */
			rsp_payload->optical_elmt_desc[2].high_alarm = trx[8];
			rsp_payload->optical_elmt_desc[2].low_alarm = trx[9];
			rsp_payload->optical_elmt_desc[2].high_warn = trx[10];
			rsp_payload->optical_elmt_desc[2].low_warn = trx[11];
			rsp_payload->optical_elmt_desc[2].element_flags =
			    cpu_to_be32(3 << 28);

			/* Optical Element Descriptor, Tx Power */
			rsp_payload->optical_elmt_desc[3].high_alarm = trx[12];
			rsp_payload->optical_elmt_desc[3].low_alarm = trx[13];
			rsp_payload->optical_elmt_desc[3].high_warn = trx[14];
			rsp_payload->optical_elmt_desc[3].low_warn = trx[15];
			rsp_payload->optical_elmt_desc[3].element_flags =
			    cpu_to_be32(4 << 28);

			/* Optical Element Descriptor, Rx Power */
			rsp_payload->optical_elmt_desc[4].high_alarm = trx[16];
			rsp_payload->optical_elmt_desc[4].low_alarm = trx[17];
			rsp_payload->optical_elmt_desc[4].high_warn = trx[18];
			rsp_payload->optical_elmt_desc[4].low_warn = trx[19];
			rsp_payload->optical_elmt_desc[4].element_flags =
			    cpu_to_be32(5 << 28);
		}

		memset(sfp, 0, SFP_RTDI_LEN);
		rval = qla2x00_read_sfp(vha, sfp_dma, sfp, 0xa2, 112, 64, 0);
		if (!rval) {
			/* Temperature high/low alarm/warning */
			rsp_payload->optical_elmt_desc[0].element_flags |=
			    cpu_to_be32(
				(sfp[0] >> 7 & 1) << 3 |
				(sfp[0] >> 6 & 1) << 2 |
				(sfp[4] >> 7 & 1) << 1 |
				(sfp[4] >> 6 & 1) << 0);

			/* Voltage high/low alarm/warning */
			rsp_payload->optical_elmt_desc[1].element_flags |=
			    cpu_to_be32(
				(sfp[0] >> 5 & 1) << 3 |
				(sfp[0] >> 4 & 1) << 2 |
				(sfp[4] >> 5 & 1) << 1 |
				(sfp[4] >> 4 & 1) << 0);

			/* Tx Bias Current high/low alarm/warning */
			rsp_payload->optical_elmt_desc[2].element_flags |=
			    cpu_to_be32(
				(sfp[0] >> 3 & 1) << 3 |
				(sfp[0] >> 2 & 1) << 2 |
				(sfp[4] >> 3 & 1) << 1 |
				(sfp[4] >> 2 & 1) << 0);

			/* Tx Power high/low alarm/warning */
			rsp_payload->optical_elmt_desc[3].element_flags |=
			    cpu_to_be32(
				(sfp[0] >> 1 & 1) << 3 |
				(sfp[0] >> 0 & 1) << 2 |
				(sfp[4] >> 1 & 1) << 1 |
				(sfp[4] >> 0 & 1) << 0);

			/* Rx Power high/low alarm/warning */
			rsp_payload->optical_elmt_desc[4].element_flags |=
			    cpu_to_be32(
				(sfp[1] >> 7 & 1) << 3 |
				(sfp[1] >> 6 & 1) << 2 |
				(sfp[5] >> 7 & 1) << 1 |
				(sfp[5] >> 6 & 1) << 0);
		}
	}

	/* Optical Product Data Descriptor */
	rsp_payload->optical_prod_desc.desc_tag = cpu_to_be32(0x10008);
	rsp_payload->optical_prod_desc.desc_len =
		cpu_to_be32(RDP_DESC_LEN(rsp_payload->optical_prod_desc));

	if (sfp) {
		memset(sfp, 0, SFP_RTDI_LEN);
		rval = qla2x00_read_sfp(vha, sfp_dma, sfp, 0xa0, 20, 64, 0);
		if (!rval) {
			memcpy(rsp_payload->optical_prod_desc.vendor_name,
			    sfp + 0,
			    sizeof(rsp_payload->optical_prod_desc.vendor_name));
			memcpy(rsp_payload->optical_prod_desc.part_number,
			    sfp + 20,
			    sizeof(rsp_payload->optical_prod_desc.part_number));
			memcpy(rsp_payload->optical_prod_desc.revision,
			    sfp + 36,
			    sizeof(rsp_payload->optical_prod_desc.revision));
			memcpy(rsp_payload->optical_prod_desc.serial_number,
			    sfp + 48,
			    sizeof(rsp_payload->optical_prod_desc.serial_number));
		}

		memset(sfp, 0, SFP_RTDI_LEN);
		rval = qla2x00_read_sfp(vha, sfp_dma, sfp, 0xa0, 84, 8, 0);
		if (!rval) {
			memcpy(rsp_payload->optical_prod_desc.date,
			    sfp + 0,
			    sizeof(rsp_payload->optical_prod_desc.date));
		}
	}

send:
	ql_dbg(ql_dbg_init, vha, 0x0183,
	    "Sending ELS Response to RDP Request...\n");
	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x0184,
	    "-------- ELS RSP -------\n");
	ql_dump_buffer(ql_dbg_init + ql_dbg_verbose, vha, 0x0185,
	    rsp_els, sizeof(*rsp_els));
	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x0186,
	    "-------- ELS RSP PAYLOAD -------\n");
	ql_dump_buffer(ql_dbg_init + ql_dbg_verbose, vha, 0x0187,
	    rsp_payload, rsp_payload_length);

	rval = qla2x00_issue_iocb(vha, rsp_els, rsp_els_dma, 0);

	if (rval) {
		ql_log(ql_log_warn, vha, 0x0188,
		    "%s: iocb failed to execute -> %x\n", __func__, rval);
	} else if (rsp_els->comp_status) {
		ql_log(ql_log_warn, vha, 0x0189,
		    "%s: iocb failed to complete -> completion=%#x subcode=(%#x,%#x)\n",
		    __func__, rsp_els->comp_status,
		    rsp_els->error_subcode_1, rsp_els->error_subcode_2);
	} else {
		ql_dbg(ql_dbg_init, vha, 0x018a, "%s: done.\n", __func__);
	}

dealloc:
	if (stat)
		dma_free_coherent(&ha->pdev->dev, sizeof(*stat),
		    stat, stat_dma);
	if (sfp)
		dma_free_coherent(&ha->pdev->dev, SFP_RTDI_LEN,
		    sfp, sfp_dma);
	if (rsp_payload)
		dma_free_coherent(&ha->pdev->dev, sizeof(*rsp_payload),
		    rsp_payload, rsp_payload_dma);
	if (rsp_els)
		dma_free_coherent(&ha->pdev->dev, sizeof(*rsp_els),
		    rsp_els, rsp_els_dma);
}

void
qla24xx_free_purex_item(struct purex_item *item)
{
	if (item == &item->vha->default_item)
		memset(&item->vha->default_item, 0, sizeof(struct purex_item));
	else
		kfree(item);
}

void qla24xx_process_purex_list(struct purex_list *list)
{
	struct list_head head = LIST_HEAD_INIT(head);
	struct purex_item *item, *next;
	ulong flags;

	spin_lock_irqsave(&list->lock, flags);
	list_splice_init(&list->head, &head);
	spin_unlock_irqrestore(&list->lock, flags);

	list_for_each_entry_safe(item, next, &head, list) {
		list_del(&item->list);
		item->process_item(item->vha, item);
		qla24xx_free_purex_item(item);
	}
}

/*
 * Context: task, can sleep
 */
void
qla83xx_idc_unlock(scsi_qla_host_t *base_vha, uint16_t requester_id)
{
#if 0
	uint16_t options = (requester_id << 15) | BIT_7;
#endif
	uint16_t retry;
	uint32_t data;
	struct qla_hw_data *ha = base_vha->hw;

	might_sleep();

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
			msleep(QLA83XX_WAIT_LOGIC_MS);
			retry++;
			ql_dbg(ql_dbg_p3p, base_vha, 0xb064,
			    "Failed to release IDC lock, retrying=%d\n", retry);
			goto retry_unlock;
		}
	} else if (retry < 10) {
		/* Retry for IDC-unlock */
		msleep(QLA83XX_WAIT_LOGIC_MS);
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
			msleep(QLA83XX_WAIT_LOGIC_MS);
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

	ql_log(ql_log_warn, base_vha, 0x015b,
	    "Disabling adapter.\n");

	if (!atomic_read(&pdev->enable_cnt)) {
		ql_log(ql_log_info, base_vha, 0xfffc,
		    "PCI device disabled, no action req for PCI error=%lx\n",
		    base_vha->pci_flags);
		return;
	}

	/*
	 * if UNLOADING flag is already set, then continue unload,
	 * where it was set first.
	 */
	if (test_and_set_bit(UNLOADING, &base_vha->dpc_flags))
		return;

	qla2x00_wait_for_sess_deletion(base_vha);

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

		if (test_and_clear_bit(DO_EEH_RECOVERY, &base_vha->dpc_flags))
			qla_pci_set_eeh_busy(base_vha);

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
		    &base_vha->dpc_flags)) {
			/* Semantic:
			 *  - NO-OP -- await next ISP-ABORT. Preferred method
			 *             to minimize disruptions that will occur
			 *             when a forced chip-reset occurs.
			 *  - Force -- ISP-ABORT scheduled.
			 */
			/* set_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags); */
		}

		if (test_and_clear_bit
		    (ISP_ABORT_NEEDED, &base_vha->dpc_flags) &&
		    !test_bit(UNLOADING, &base_vha->dpc_flags)) {
			bool do_reset = true;

			switch (base_vha->qlini_mode) {
			case QLA2XXX_INI_MODE_ENABLED:
				break;
			case QLA2XXX_INI_MODE_DISABLED:
				if (!qla_tgt_mode_enabled(base_vha) &&
				    !ha->flags.fw_started)
					do_reset = false;
				break;
			case QLA2XXX_INI_MODE_DUAL:
				if (!qla_dual_mode_enabled(base_vha) &&
				    !ha->flags.fw_started)
					do_reset = false;
				break;
			default:
				break;
			}

			if (do_reset && !(test_and_set_bit(ABORT_ISP_ACTIVE,
			    &base_vha->dpc_flags))) {
				base_vha->flags.online = 1;
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

		if (test_bit(PROCESS_PUREX_IOCB, &base_vha->dpc_flags)) {
			if (atomic_read(&base_vha->loop_state) == LOOP_READY) {
				qla24xx_process_purex_list
					(&base_vha->purex_list);
				clear_bit(PROCESS_PUREX_IOCB,
				    &base_vha->dpc_flags);
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
		if (!qla2x00_reset_active(base_vha) &&
		    test_and_clear_bit(LOOP_RESYNC_NEEDED,
		    &base_vha->dpc_flags)) {
			/*
			 * Allow abort_isp to complete before moving on to scanning.
			 */
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

		if (test_and_clear_bit(SET_ZIO_THRESHOLD_NEEDED,
				       &base_vha->dpc_flags)) {
			u16 threshold = ha->nvme_last_rptd_aen + ha->last_zio_threshold;

			if (threshold > ha->orig_fw_xcb_count)
				threshold = ha->orig_fw_xcb_count;

			ql_log(ql_log_info, base_vha, 0xffffff,
			       "SET ZIO Activity exchange threshold to %d.\n",
			       threshold);
			if (qla27xx_set_zio_threshold(base_vha, threshold)) {
				ql_log(ql_log_info, base_vha, 0xffffff,
				       "Unable to SET ZIO Activity exchange threshold to %d.\n",
				       threshold);
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

static bool qla_do_heartbeat(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	u32 cmpl_cnt;
	u16 i;
	bool do_heartbeat = false;

	/*
	 * Allow do_heartbeat only if we don’t have any active interrupts,
	 * but there are still IOs outstanding with firmware.
	 */
	cmpl_cnt = ha->base_qpair->cmd_completion_cnt;
	if (cmpl_cnt == ha->base_qpair->prev_completion_cnt &&
	    cmpl_cnt != ha->base_qpair->cmd_cnt) {
		do_heartbeat = true;
		goto skip;
	}
	ha->base_qpair->prev_completion_cnt = cmpl_cnt;

	for (i = 0; i < ha->max_qpairs; i++) {
		if (ha->queue_pair_map[i]) {
			cmpl_cnt = ha->queue_pair_map[i]->cmd_completion_cnt;
			if (cmpl_cnt == ha->queue_pair_map[i]->prev_completion_cnt &&
			    cmpl_cnt != ha->queue_pair_map[i]->cmd_cnt) {
				do_heartbeat = true;
				break;
			}
			ha->queue_pair_map[i]->prev_completion_cnt = cmpl_cnt;
		}
	}

skip:
	return do_heartbeat;
}

static void qla_heart_beat(struct scsi_qla_host *vha, u16 dpc_started)
{
	struct qla_hw_data *ha = vha->hw;

	if (vha->vp_idx)
		return;

	if (vha->hw->flags.eeh_busy || qla2x00_chip_is_down(vha))
		return;

	/*
	 * dpc thread cannot run if heartbeat is running at the same time.
	 * We also do not want to starve heartbeat task. Therefore, do
	 * heartbeat task at least once every 5 seconds.
	 */
	if (dpc_started &&
	    time_before(jiffies, ha->last_heartbeat_run_jiffies + 5 * HZ))
		return;

	if (qla_do_heartbeat(vha)) {
		ha->last_heartbeat_run_jiffies = jiffies;
		queue_work(ha->wq, &ha->heartbeat_work);
	}
}

static void qla_wind_down_chip(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (!ha->flags.eeh_busy)
		return;
	if (ha->pci_error_state)
		/* system is trying to recover */
		return;

	/*
	 * Current system is not handling PCIE error.  At this point, this is
	 * best effort to wind down the adapter.
	 */
	if (time_after_eq(jiffies, ha->eeh_jif + ql2xdelay_before_pci_error_handling * HZ) &&
	    !ha->flags.eeh_flush) {
		ql_log(ql_log_info, vha, 0x9009,
		    "PCI Error detected, attempting to reset hardware.\n");

		ha->isp_ops->reset_chip(vha);
		ha->isp_ops->disable_intrs(ha);

		ha->flags.eeh_flush = EEH_FLUSH_RDY;
		ha->eeh_jif = jiffies;

	} else if (ha->flags.eeh_flush == EEH_FLUSH_RDY &&
	    time_after_eq(jiffies, ha->eeh_jif +  5 * HZ)) {
		pci_clear_master(ha->pdev);

		/* flush all command */
		qla2x00_abort_isp_cleanup(vha);
		ha->flags.eeh_flush = EEH_FLUSH_DONE;

		ql_log(ql_log_info, vha, 0x900a,
		    "PCI Error handling complete, all IOs aborted.\n");
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
	unsigned long flags;
	fc_port_t *fcport = NULL;

	if (ha->flags.eeh_busy) {
		qla_wind_down_chip(vha);

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

	if (vha->link_down_time < QLA2XX_MAX_LINK_DOWN_TIME)
		vha->link_down_time++;

	spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->tgt_link_down_time < QLA2XX_MAX_LINK_DOWN_TIME)
			fcport->tgt_link_down_time++;
	}
	spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);

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
			if (!(vha->device_flags & DFLG_NO_CABLE) && !vha->vp_idx) {
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

	/* check if edif running */
	if (vha->hw->flags.edif_enabled)
		qla_edif_timer(vha);

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
	index = atomic_read(&ha->nvme_active_aen_cnt);
	if (!vha->vp_idx &&
	    (index != ha->nvme_last_rptd_aen) &&
	    ha->zio_mode == QLA_ZIO_MODE_6 &&
	    !ha->flags.host_shutting_down) {
		ha->nvme_last_rptd_aen = atomic_read(&ha->nvme_active_aen_cnt);
		ql_log(ql_log_info, vha, 0x3002,
		    "nvme: Sched: Set ZIO exchange threshold to %d.\n",
		    ha->nvme_last_rptd_aen);
		set_bit(SET_ZIO_THRESHOLD_NEEDED, &vha->dpc_flags);
		start_dpc++;
	}

	if (!vha->vp_idx &&
	    atomic_read(&ha->zio_threshold) != ha->last_zio_threshold &&
	    IS_ZIO_THRESHOLD_CAPABLE(ha)) {
		ql_log(ql_log_info, vha, 0x3002,
		    "Sched: Set ZIO exchange threshold to %d.\n",
		    ha->last_zio_threshold);
		ha->last_zio_threshold = atomic_read(&ha->zio_threshold);
		set_bit(SET_ZIO_THRESHOLD_NEEDED, &vha->dpc_flags);
		start_dpc++;
	}

	/* borrowing w to signify dpc will run */
	w = 0;
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
	    test_bit(RELOGIN_NEEDED, &vha->dpc_flags) ||
	    test_bit(PROCESS_PUREX_IOCB, &vha->dpc_flags))) {
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
		    "relogin_needed=%d, Process_purex_iocb=%d.\n",
		    test_bit(BEACON_BLINK_NEEDED, &vha->dpc_flags),
		    test_bit(ISP_UNRECOVERABLE, &vha->dpc_flags),
		    test_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags),
		    test_bit(VP_DPC_NEEDED, &vha->dpc_flags),
		    test_bit(RELOGIN_NEEDED, &vha->dpc_flags),
		    test_bit(PROCESS_PUREX_IOCB, &vha->dpc_flags));
		qla2xxx_wake_dpc(vha);
		w = 1;
	}

	qla_heart_beat(vha, w);

	qla2x00_restart_timer(vha, WATCH_INTERVAL);
}

/* Firmware interface routines. */

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
#define FW_ISP28XX	11

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
#define FW_FILE_ISP28XX	"ql2800_fw.bin"


static DEFINE_MUTEX(qla_fw_lock);

static struct fw_blob qla_fw_blobs[] = {
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
	{ .name = FW_FILE_ISP28XX, },
	{ .name = NULL, },
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
	} else if (IS_QLA28XX(ha)) {
		blob = &qla_fw_blobs[FW_ISP28XX];
	} else {
		return NULL;
	}

	if (!blob->name)
		return NULL;

	mutex_lock(&qla_fw_lock);
	if (blob->fw)
		goto out;

	if (request_firmware(&blob->fw, blob->name, &ha->pdev->dev)) {
		ql_log(ql_log_warn, vha, 0x0063,
		    "Failed to load firmware image (%s).\n", blob->name);
		blob->fw = NULL;
		blob = NULL;
	}

out:
	mutex_unlock(&qla_fw_lock);
	return blob;
}

static void
qla2x00_release_firmware(void)
{
	struct fw_blob *blob;

	mutex_lock(&qla_fw_lock);
	for (blob = qla_fw_blobs; blob->name; blob++)
		release_firmware(blob->fw);
	mutex_unlock(&qla_fw_lock);
}

static void qla_pci_error_cleanup(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);
	struct qla_qpair *qpair = NULL;
	struct scsi_qla_host *vp, *tvp;
	fc_port_t *fcport;
	int i;
	unsigned long flags;

	ql_dbg(ql_dbg_aer, vha, 0x9000,
	       "%s\n", __func__);
	ha->chip_reset++;

	ha->base_qpair->chip_reset = ha->chip_reset;
	for (i = 0; i < ha->max_qpairs; i++) {
		if (ha->queue_pair_map[i])
			ha->queue_pair_map[i]->chip_reset =
			    ha->base_qpair->chip_reset;
	}

	/*
	 * purge mailbox might take a while. Slot Reset/chip reset
	 * will take care of the purge
	 */

	mutex_lock(&ha->mq_lock);
	ha->base_qpair->online = 0;
	list_for_each_entry(qpair, &base_vha->qp_list, qp_list_elem)
		qpair->online = 0;
	wmb();
	mutex_unlock(&ha->mq_lock);

	qla2x00_mark_all_devices_lost(vha);

	spin_lock_irqsave(&ha->vport_slock, flags);
	list_for_each_entry_safe(vp, tvp, &ha->vp_list, list) {
		atomic_inc(&vp->vref_count);
		spin_unlock_irqrestore(&ha->vport_slock, flags);
		qla2x00_mark_all_devices_lost(vp);
		spin_lock_irqsave(&ha->vport_slock, flags);
		atomic_dec(&vp->vref_count);
	}
	spin_unlock_irqrestore(&ha->vport_slock, flags);

	/* Clear all async request states across all VPs. */
	list_for_each_entry(fcport, &vha->vp_fcports, list)
		fcport->flags &= ~(FCF_LOGIN_NEEDED | FCF_ASYNC_SENT);

	spin_lock_irqsave(&ha->vport_slock, flags);
	list_for_each_entry_safe(vp, tvp, &ha->vp_list, list) {
		atomic_inc(&vp->vref_count);
		spin_unlock_irqrestore(&ha->vport_slock, flags);
		list_for_each_entry(fcport, &vp->vp_fcports, list)
			fcport->flags &= ~(FCF_LOGIN_NEEDED | FCF_ASYNC_SENT);
		spin_lock_irqsave(&ha->vport_slock, flags);
		atomic_dec(&vp->vref_count);
	}
	spin_unlock_irqrestore(&ha->vport_slock, flags);
}


static pci_ers_result_t
qla2xxx_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	scsi_qla_host_t *vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = vha->hw;
	pci_ers_result_t ret = PCI_ERS_RESULT_NEED_RESET;

	ql_log(ql_log_warn, vha, 0x9000,
	       "PCI error detected, state %x.\n", state);
	ha->pci_error_state = QLA_PCI_ERR_DETECTED;

	if (!atomic_read(&pdev->enable_cnt)) {
		ql_log(ql_log_info, vha, 0xffff,
			"PCI device is disabled,state %x\n", state);
		ret = PCI_ERS_RESULT_NEED_RESET;
		goto out;
	}

	switch (state) {
	case pci_channel_io_normal:
		qla_pci_set_eeh_busy(vha);
		if (ql2xmqsupport || ql2xnvmeenable) {
			set_bit(QPAIR_ONLINE_CHECK_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		ret = PCI_ERS_RESULT_CAN_RECOVER;
		break;
	case pci_channel_io_frozen:
		qla_pci_set_eeh_busy(vha);
		ret = PCI_ERS_RESULT_NEED_RESET;
		break;
	case pci_channel_io_perm_failure:
		ha->flags.pci_channel_io_perm_failure = 1;
		qla2x00_abort_all_cmds(vha, DID_NO_CONNECT << 16);
		if (ql2xmqsupport || ql2xnvmeenable) {
			set_bit(QPAIR_ONLINE_CHECK_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		ret = PCI_ERS_RESULT_DISCONNECT;
	}
out:
	ql_dbg(ql_dbg_aer, vha, 0x600d,
	       "PCI error detected returning [%x].\n", ret);
	return ret;
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

	ql_log(ql_log_warn, base_vha, 0x9000,
	       "mmio enabled\n");

	ha->pci_error_state = QLA_PCI_MMIO_ENABLED;

	if (IS_QLA82XX(ha))
		return PCI_ERS_RESULT_RECOVERED;

	if (qla2x00_isp_reg_stat(ha)) {
		ql_log(ql_log_info, base_vha, 0x803f,
		    "During mmio enabled, PCI/Register disconnect still detected.\n");
		goto out;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (IS_QLA2100(ha) || IS_QLA2200(ha)){
		stat = rd_reg_word(&reg->hccr);
		if (stat & HCCR_RISC_PAUSE)
			risc_paused = 1;
	} else if (IS_QLA23XX(ha)) {
		stat = rd_reg_dword(&reg->u.isp2300.host_status);
		if (stat & HSR_RISC_PAUSED)
			risc_paused = 1;
	} else if (IS_FWI2_CAPABLE(ha)) {
		stat = rd_reg_dword(&reg24->host_status);
		if (stat & HSRX_RISC_PAUSED)
			risc_paused = 1;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (risc_paused) {
		ql_log(ql_log_info, base_vha, 0x9003,
		    "RISC paused -- mmio_enabled, Dumping firmware.\n");
		qla2xxx_dump_fw(base_vha);
	}
out:
	/* set PCI_ERS_RESULT_NEED_RESET to trigger call to qla2xxx_pci_slot_reset */
	ql_dbg(ql_dbg_aer, base_vha, 0x600d,
	       "mmio enabled returning.\n");
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t
qla2xxx_pci_slot_reset(struct pci_dev *pdev)
{
	pci_ers_result_t ret = PCI_ERS_RESULT_DISCONNECT;
	scsi_qla_host_t *base_vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = base_vha->hw;
	int rc;
	struct qla_qpair *qpair = NULL;

	ql_log(ql_log_warn, base_vha, 0x9004,
	       "Slot Reset.\n");

	ha->pci_error_state = QLA_PCI_SLOT_RESET;
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


	if (ha->isp_ops->pci_config(base_vha))
		goto exit_slot_reset;

	mutex_lock(&ha->mq_lock);
	list_for_each_entry(qpair, &base_vha->qp_list, qp_list_elem)
		qpair->online = 1;
	mutex_unlock(&ha->mq_lock);

	ha->flags.eeh_busy = 0;
	base_vha->flags.online = 1;
	set_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
	ha->isp_ops->abort_isp(base_vha);
	clear_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);

	if (qla2x00_isp_reg_stat(ha)) {
		ha->flags.eeh_busy = 1;
		qla_pci_error_cleanup(base_vha);
		ql_log(ql_log_warn, base_vha, 0x9005,
		       "Device unable to recover from PCI error.\n");
	} else {
		ret =  PCI_ERS_RESULT_RECOVERED;
	}

exit_slot_reset:
	ql_dbg(ql_dbg_aer, base_vha, 0x900e,
	    "Slot Reset returning %x.\n", ret);

	return ret;
}

static void
qla2xxx_pci_resume(struct pci_dev *pdev)
{
	scsi_qla_host_t *base_vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = base_vha->hw;
	int ret;

	ql_log(ql_log_warn, base_vha, 0x900f,
	       "Pci Resume.\n");


	ret = qla2x00_wait_for_hba_online(base_vha);
	if (ret != QLA_SUCCESS) {
		ql_log(ql_log_fatal, base_vha, 0x9002,
		    "The device failed to resume I/O from slot/link_reset.\n");
	}
	ha->pci_error_state = QLA_PCI_RESUME;
	ql_dbg(ql_dbg_aer, base_vha, 0x600d,
	       "Pci Resume returning.\n");
}

void qla_pci_set_eeh_busy(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	bool do_cleanup = false;
	unsigned long flags;

	if (ha->flags.eeh_busy)
		return;

	spin_lock_irqsave(&base_vha->work_lock, flags);
	if (!ha->flags.eeh_busy) {
		ha->eeh_jif = jiffies;
		ha->flags.eeh_flush = 0;

		ha->flags.eeh_busy = 1;
		do_cleanup = true;
	}
	spin_unlock_irqrestore(&base_vha->work_lock, flags);

	if (do_cleanup)
		qla_pci_error_cleanup(base_vha);
}

/*
 * this routine will schedule a task to pause IO from interrupt context
 * if caller sees a PCIE error event (register read = 0xf's)
 */
void qla_schedule_eeh_work(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	if (ha->flags.eeh_busy)
		return;

	set_bit(DO_EEH_RECOVERY, &base_vha->dpc_flags);
	qla2xxx_wake_dpc(base_vha);
}

static void
qla_pci_reset_prepare(struct pci_dev *pdev)
{
	scsi_qla_host_t *base_vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = base_vha->hw;
	struct qla_qpair *qpair;

	ql_log(ql_log_warn, base_vha, 0xffff,
	    "%s.\n", __func__);

	/*
	 * PCI FLR/function reset is about to reset the
	 * slot. Stop the chip to stop all DMA access.
	 * It is assumed that pci_reset_done will be called
	 * after FLR to resume Chip operation.
	 */
	ha->flags.eeh_busy = 1;
	mutex_lock(&ha->mq_lock);
	list_for_each_entry(qpair, &base_vha->qp_list, qp_list_elem)
		qpair->online = 0;
	mutex_unlock(&ha->mq_lock);

	set_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
	qla2x00_abort_isp_cleanup(base_vha);
	qla2x00_abort_all_cmds(base_vha, DID_RESET << 16);
}

static void
qla_pci_reset_done(struct pci_dev *pdev)
{
	scsi_qla_host_t *base_vha = pci_get_drvdata(pdev);
	struct qla_hw_data *ha = base_vha->hw;
	struct qla_qpair *qpair;

	ql_log(ql_log_warn, base_vha, 0xffff,
	    "%s.\n", __func__);

	/*
	 * FLR just completed by PCI layer. Resume adapter
	 */
	ha->flags.eeh_busy = 0;
	mutex_lock(&ha->mq_lock);
	list_for_each_entry(qpair, &base_vha->qp_list, qp_list_elem)
		qpair->online = 1;
	mutex_unlock(&ha->mq_lock);

	base_vha->flags.online = 1;
	ha->isp_ops->abort_isp(base_vha);
	clear_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
}

static void qla2xxx_map_queues(struct Scsi_Host *shost)
{
	scsi_qla_host_t *vha = (scsi_qla_host_t *)shost->hostdata;
	struct blk_mq_queue_map *qmap = &shost->tag_set.map[HCTX_TYPE_DEFAULT];

	if (USER_CTRL_IRQ(vha->hw) || !vha->hw->mqiobase)
		blk_mq_map_queues(qmap);
	else
		blk_mq_pci_map_queues(qmap, vha->hw->pdev, vha->irq_offset);
}

struct scsi_host_template qla2xxx_driver_template = {
	.module			= THIS_MODULE,
	.name			= QLA2XXX_DRIVER_NAME,
	.queuecommand		= qla2xxx_queuecommand,

	.eh_timed_out		= fc_eh_timed_out,
	.eh_abort_handler	= qla2xxx_eh_abort,
	.eh_should_retry_cmd	= fc_eh_should_retry_cmd,
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
	.sg_tablesize		= SG_ALL,

	.max_sectors		= 0xFFFF,
	.shost_groups		= qla2x00_host_groups,

	.supported_mode		= MODE_INITIATOR,
	.track_queue_depth	= 1,
	.cmd_size		= sizeof(srb_t),
};

static const struct pci_error_handlers qla2xxx_err_handler = {
	.error_detected = qla2xxx_pci_error_detected,
	.mmio_enabled = qla2xxx_pci_mmio_enabled,
	.slot_reset = qla2xxx_pci_slot_reset,
	.resume = qla2xxx_pci_resume,
	.reset_prepare = qla_pci_reset_prepare,
	.reset_done = qla_pci_reset_done,
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
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2061) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2081) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2281) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2089) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2289) },
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

	BUILD_BUG_ON(sizeof(cmd_a64_entry_t) != 64);
	BUILD_BUG_ON(sizeof(cmd_entry_t) != 64);
	BUILD_BUG_ON(sizeof(cont_a64_entry_t) != 64);
	BUILD_BUG_ON(sizeof(cont_entry_t) != 64);
	BUILD_BUG_ON(sizeof(init_cb_t) != 96);
	BUILD_BUG_ON(sizeof(mrk_entry_t) != 64);
	BUILD_BUG_ON(sizeof(ms_iocb_entry_t) != 64);
	BUILD_BUG_ON(sizeof(request_t) != 64);
	BUILD_BUG_ON(sizeof(struct abort_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct abort_iocb_entry_fx00) != 64);
	BUILD_BUG_ON(sizeof(struct abts_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct access_chip_84xx) != 64);
	BUILD_BUG_ON(sizeof(struct access_chip_rsp_84xx) != 64);
	BUILD_BUG_ON(sizeof(struct cmd_bidir) != 64);
	BUILD_BUG_ON(sizeof(struct cmd_nvme) != 64);
	BUILD_BUG_ON(sizeof(struct cmd_type_6) != 64);
	BUILD_BUG_ON(sizeof(struct cmd_type_7) != 64);
	BUILD_BUG_ON(sizeof(struct cmd_type_7_fx00) != 64);
	BUILD_BUG_ON(sizeof(struct cmd_type_crc_2) != 64);
	BUILD_BUG_ON(sizeof(struct ct_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct ct_fdmi1_hba_attributes) != 2604);
	BUILD_BUG_ON(sizeof(struct ct_fdmi2_hba_attributes) != 4424);
	BUILD_BUG_ON(sizeof(struct ct_fdmi2_port_attributes) != 4164);
	BUILD_BUG_ON(sizeof(struct ct_fdmi_hba_attr) != 260);
	BUILD_BUG_ON(sizeof(struct ct_fdmi_port_attr) != 260);
	BUILD_BUG_ON(sizeof(struct ct_rsp_hdr) != 16);
	BUILD_BUG_ON(sizeof(struct ctio_crc2_to_fw) != 64);
	BUILD_BUG_ON(sizeof(struct device_reg_24xx) != 256);
	BUILD_BUG_ON(sizeof(struct device_reg_25xxmq) != 24);
	BUILD_BUG_ON(sizeof(struct device_reg_2xxx) != 256);
	BUILD_BUG_ON(sizeof(struct device_reg_82xx) != 1288);
	BUILD_BUG_ON(sizeof(struct device_reg_fx00) != 216);
	BUILD_BUG_ON(sizeof(struct els_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct els_sts_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct fxdisc_entry_fx00) != 64);
	BUILD_BUG_ON(sizeof(struct imm_ntfy_from_isp) != 64);
	BUILD_BUG_ON(sizeof(struct init_cb_24xx) != 128);
	BUILD_BUG_ON(sizeof(struct init_cb_81xx) != 128);
	BUILD_BUG_ON(sizeof(struct logio_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct mbx_entry) != 64);
	BUILD_BUG_ON(sizeof(struct mid_init_cb_24xx) != 5252);
	BUILD_BUG_ON(sizeof(struct mrk_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct nvram_24xx) != 512);
	BUILD_BUG_ON(sizeof(struct nvram_81xx) != 512);
	BUILD_BUG_ON(sizeof(struct pt_ls4_request) != 64);
	BUILD_BUG_ON(sizeof(struct pt_ls4_rx_unsol) != 64);
	BUILD_BUG_ON(sizeof(struct purex_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct qla2100_fw_dump) != 123634);
	BUILD_BUG_ON(sizeof(struct qla2300_fw_dump) != 136100);
	BUILD_BUG_ON(sizeof(struct qla24xx_fw_dump) != 37976);
	BUILD_BUG_ON(sizeof(struct qla25xx_fw_dump) != 39228);
	BUILD_BUG_ON(sizeof(struct qla2xxx_fce_chain) != 52);
	BUILD_BUG_ON(sizeof(struct qla2xxx_fw_dump) != 136172);
	BUILD_BUG_ON(sizeof(struct qla2xxx_mq_chain) != 524);
	BUILD_BUG_ON(sizeof(struct qla2xxx_mqueue_chain) != 8);
	BUILD_BUG_ON(sizeof(struct qla2xxx_mqueue_header) != 12);
	BUILD_BUG_ON(sizeof(struct qla2xxx_offld_chain) != 24);
	BUILD_BUG_ON(sizeof(struct qla81xx_fw_dump) != 39420);
	BUILD_BUG_ON(sizeof(struct qla82xx_uri_data_desc) != 28);
	BUILD_BUG_ON(sizeof(struct qla82xx_uri_table_desc) != 32);
	BUILD_BUG_ON(sizeof(struct qla83xx_fw_dump) != 51196);
	BUILD_BUG_ON(sizeof(struct qla_fcp_prio_cfg) != FCP_PRIO_CFG_SIZE);
	BUILD_BUG_ON(sizeof(struct qla_fdt_layout) != 128);
	BUILD_BUG_ON(sizeof(struct qla_flt_header) != 8);
	BUILD_BUG_ON(sizeof(struct qla_flt_region) != 16);
	BUILD_BUG_ON(sizeof(struct qla_npiv_entry) != 24);
	BUILD_BUG_ON(sizeof(struct qla_npiv_header) != 16);
	BUILD_BUG_ON(sizeof(struct rdp_rsp_payload) != 336);
	BUILD_BUG_ON(sizeof(struct sns_cmd_pkt) != 2064);
	BUILD_BUG_ON(sizeof(struct sts_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct tsk_mgmt_entry) != 64);
	BUILD_BUG_ON(sizeof(struct tsk_mgmt_entry_fx00) != 64);
	BUILD_BUG_ON(sizeof(struct verify_chip_entry_84xx) != 64);
	BUILD_BUG_ON(sizeof(struct verify_chip_rsp_84xx) != 52);
	BUILD_BUG_ON(sizeof(struct vf_evfp_entry_24xx) != 56);
	BUILD_BUG_ON(sizeof(struct vp_config_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct vp_ctrl_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(struct vp_rpt_id_entry_24xx) != 64);
	BUILD_BUG_ON(sizeof(sts21_entry_t) != 64);
	BUILD_BUG_ON(sizeof(sts22_entry_t) != 64);
	BUILD_BUG_ON(sizeof(sts_cont_entry_t) != 64);
	BUILD_BUG_ON(sizeof(sts_entry_t) != 64);
	BUILD_BUG_ON(sizeof(sw_info_t) != 32);
	BUILD_BUG_ON(sizeof(target_id_t) != 2);

	qla_trace_init();

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

	qla_trace_uninit();
	return ret;
}

/**
 * qla2x00_module_exit - Module cleanup.
 **/
static void __exit
qla2x00_module_exit(void)
{
	pci_unregister_driver(&qla2xxx_pci_driver);
	qla2x00_release_firmware();
	kmem_cache_destroy(ctx_cachep);
	fc_release_transport(qla2xxx_transport_vport_template);
	if (apidev_major >= 0)
		unregister_chrdev(apidev_major, QLA2XXX_APIDEV);
	fc_release_transport(qla2xxx_transport_template);
	qlt_exit();
	kmem_cache_destroy(srb_cachep);
	qla_trace_uninit();
}

module_init(qla2x00_module_init);
module_exit(qla2x00_module_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic Fibre Channel HBA Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FW_FILE_ISP21XX);
MODULE_FIRMWARE(FW_FILE_ISP22XX);
MODULE_FIRMWARE(FW_FILE_ISP2300);
MODULE_FIRMWARE(FW_FILE_ISP2322);
MODULE_FIRMWARE(FW_FILE_ISP24XX);
MODULE_FIRMWARE(FW_FILE_ISP25XX);

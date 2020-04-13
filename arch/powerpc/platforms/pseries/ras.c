// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001 Dave Engebretsen IBM Corporation
 */

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/irq_work.h>

#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/firmware.h>
#include <asm/mce.h>

#include "pseries.h"

static unsigned char ras_log_buf[RTAS_ERROR_LOG_MAX];
static DEFINE_SPINLOCK(ras_log_buf_lock);

static int ras_check_exception_token;

static void mce_process_errlog_event(struct irq_work *work);
static struct irq_work mce_errlog_process_work = {
	.func = mce_process_errlog_event,
};

#define EPOW_SENSOR_TOKEN	9
#define EPOW_SENSOR_INDEX	0

/* EPOW events counter variable */
static int num_epow_events;

static irqreturn_t ras_hotplug_interrupt(int irq, void *dev_id);
static irqreturn_t ras_epow_interrupt(int irq, void *dev_id);
static irqreturn_t ras_error_interrupt(int irq, void *dev_id);

/* RTAS pseries MCE errorlog section. */
struct pseries_mc_errorlog {
	__be32	fru_id;
	__be32	proc_id;
	u8	error_type;
	/*
	 * sub_err_type (1 byte). Bit fields depends on error_type
	 *
	 *   MSB0
	 *   |
	 *   V
	 *   01234567
	 *   XXXXXXXX
	 *
	 * For error_type == MC_ERROR_TYPE_UE
	 *   XXXXXXXX
	 *   X		1: Permanent or Transient UE.
	 *    X		1: Effective address provided.
	 *     X	1: Logical address provided.
	 *      XX	2: Reserved.
	 *        XXX	3: Type of UE error.
	 *
	 * For error_type != MC_ERROR_TYPE_UE
	 *   XXXXXXXX
	 *   X		1: Effective address provided.
	 *    XXXXX	5: Reserved.
	 *         XX	2: Type of SLB/ERAT/TLB error.
	 */
	u8	sub_err_type;
	u8	reserved_1[6];
	__be64	effective_address;
	__be64	logical_address;
} __packed;

/* RTAS pseries MCE error types */
#define MC_ERROR_TYPE_UE		0x00
#define MC_ERROR_TYPE_SLB		0x01
#define MC_ERROR_TYPE_ERAT		0x02
#define MC_ERROR_TYPE_UNKNOWN		0x03
#define MC_ERROR_TYPE_TLB		0x04
#define MC_ERROR_TYPE_D_CACHE		0x05
#define MC_ERROR_TYPE_I_CACHE		0x07

/* RTAS pseries MCE error sub types */
#define MC_ERROR_UE_INDETERMINATE		0
#define MC_ERROR_UE_IFETCH			1
#define MC_ERROR_UE_PAGE_TABLE_WALK_IFETCH	2
#define MC_ERROR_UE_LOAD_STORE			3
#define MC_ERROR_UE_PAGE_TABLE_WALK_LOAD_STORE	4

#define UE_EFFECTIVE_ADDR_PROVIDED		0x40
#define UE_LOGICAL_ADDR_PROVIDED		0x20

#define MC_ERROR_SLB_PARITY		0
#define MC_ERROR_SLB_MULTIHIT		1
#define MC_ERROR_SLB_INDETERMINATE	2

#define MC_ERROR_ERAT_PARITY		1
#define MC_ERROR_ERAT_MULTIHIT		2
#define MC_ERROR_ERAT_INDETERMINATE	3

#define MC_ERROR_TLB_PARITY		1
#define MC_ERROR_TLB_MULTIHIT		2
#define MC_ERROR_TLB_INDETERMINATE	3

static inline u8 rtas_mc_error_sub_type(const struct pseries_mc_errorlog *mlog)
{
	switch (mlog->error_type) {
	case	MC_ERROR_TYPE_UE:
		return (mlog->sub_err_type & 0x07);
	case	MC_ERROR_TYPE_SLB:
	case	MC_ERROR_TYPE_ERAT:
	case	MC_ERROR_TYPE_TLB:
		return (mlog->sub_err_type & 0x03);
	default:
		return 0;
	}
}

/*
 * Enable the hotplug interrupt late because processing them may touch other
 * devices or systems (e.g. hugepages) that have not been initialized at the
 * subsys stage.
 */
int __init init_ras_hotplug_IRQ(void)
{
	struct device_node *np;

	/* Hotplug Events */
	np = of_find_node_by_path("/event-sources/hot-plug-events");
	if (np != NULL) {
		if (dlpar_workqueue_init() == 0)
			request_event_sources_irqs(np, ras_hotplug_interrupt,
						   "RAS_HOTPLUG");
		of_node_put(np);
	}

	return 0;
}
machine_late_initcall(pseries, init_ras_hotplug_IRQ);

/*
 * Initialize handlers for the set of interrupts caused by hardware errors
 * and power system events.
 */
static int __init init_ras_IRQ(void)
{
	struct device_node *np;

	ras_check_exception_token = rtas_token("check-exception");

	/* Internal Errors */
	np = of_find_node_by_path("/event-sources/internal-errors");
	if (np != NULL) {
		request_event_sources_irqs(np, ras_error_interrupt,
					   "RAS_ERROR");
		of_node_put(np);
	}

	/* EPOW Events */
	np = of_find_node_by_path("/event-sources/epow-events");
	if (np != NULL) {
		request_event_sources_irqs(np, ras_epow_interrupt, "RAS_EPOW");
		of_node_put(np);
	}

	return 0;
}
machine_subsys_initcall(pseries, init_ras_IRQ);

#define EPOW_SHUTDOWN_NORMAL				1
#define EPOW_SHUTDOWN_ON_UPS				2
#define EPOW_SHUTDOWN_LOSS_OF_CRITICAL_FUNCTIONS	3
#define EPOW_SHUTDOWN_AMBIENT_TEMPERATURE_TOO_HIGH	4

static void handle_system_shutdown(char event_modifier)
{
	switch (event_modifier) {
	case EPOW_SHUTDOWN_NORMAL:
		pr_emerg("Power off requested\n");
		orderly_poweroff(true);
		break;

	case EPOW_SHUTDOWN_ON_UPS:
		pr_emerg("Loss of system power detected. System is running on"
			 " UPS/battery. Check RTAS error log for details\n");
		orderly_poweroff(true);
		break;

	case EPOW_SHUTDOWN_LOSS_OF_CRITICAL_FUNCTIONS:
		pr_emerg("Loss of system critical functions detected. Check"
			 " RTAS error log for details\n");
		orderly_poweroff(true);
		break;

	case EPOW_SHUTDOWN_AMBIENT_TEMPERATURE_TOO_HIGH:
		pr_emerg("High ambient temperature detected. Check RTAS"
			 " error log for details\n");
		orderly_poweroff(true);
		break;

	default:
		pr_err("Unknown power/cooling shutdown event (modifier = %d)\n",
			event_modifier);
	}
}

struct epow_errorlog {
	unsigned char sensor_value;
	unsigned char event_modifier;
	unsigned char extended_modifier;
	unsigned char reserved;
	unsigned char platform_reason;
};

#define EPOW_RESET			0
#define EPOW_WARN_COOLING		1
#define EPOW_WARN_POWER			2
#define EPOW_SYSTEM_SHUTDOWN		3
#define EPOW_SYSTEM_HALT		4
#define EPOW_MAIN_ENCLOSURE		5
#define EPOW_POWER_OFF			7

static void rtas_parse_epow_errlog(struct rtas_error_log *log)
{
	struct pseries_errorlog *pseries_log;
	struct epow_errorlog *epow_log;
	char action_code;
	char modifier;

	pseries_log = get_pseries_errorlog(log, PSERIES_ELOG_SECT_ID_EPOW);
	if (pseries_log == NULL)
		return;

	epow_log = (struct epow_errorlog *)pseries_log->data;
	action_code = epow_log->sensor_value & 0xF;	/* bottom 4 bits */
	modifier = epow_log->event_modifier & 0xF;	/* bottom 4 bits */

	switch (action_code) {
	case EPOW_RESET:
		if (num_epow_events) {
			pr_info("Non critical power/cooling issue cleared\n");
			num_epow_events--;
		}
		break;

	case EPOW_WARN_COOLING:
		pr_info("Non-critical cooling issue detected. Check RTAS error"
			" log for details\n");
		break;

	case EPOW_WARN_POWER:
		pr_info("Non-critical power issue detected. Check RTAS error"
			" log for details\n");
		break;

	case EPOW_SYSTEM_SHUTDOWN:
		handle_system_shutdown(modifier);
		break;

	case EPOW_SYSTEM_HALT:
		pr_emerg("Critical power/cooling issue detected. Check RTAS"
			 " error log for details. Powering off.\n");
		orderly_poweroff(true);
		break;

	case EPOW_MAIN_ENCLOSURE:
	case EPOW_POWER_OFF:
		pr_emerg("System about to lose power. Check RTAS error log "
			 " for details. Powering off immediately.\n");
		emergency_sync();
		kernel_power_off();
		break;

	default:
		pr_err("Unknown power/cooling event (action code  = %d)\n",
			action_code);
	}

	/* Increment epow events counter variable */
	if (action_code != EPOW_RESET)
		num_epow_events++;
}

static irqreturn_t ras_hotplug_interrupt(int irq, void *dev_id)
{
	struct pseries_errorlog *pseries_log;
	struct pseries_hp_errorlog *hp_elog;

	spin_lock(&ras_log_buf_lock);

	rtas_call(ras_check_exception_token, 6, 1, NULL,
		  RTAS_VECTOR_EXTERNAL_INTERRUPT, virq_to_hw(irq),
		  RTAS_HOTPLUG_EVENTS, 0, __pa(&ras_log_buf),
		  rtas_get_error_log_max());

	pseries_log = get_pseries_errorlog((struct rtas_error_log *)ras_log_buf,
					   PSERIES_ELOG_SECT_ID_HOTPLUG);
	hp_elog = (struct pseries_hp_errorlog *)pseries_log->data;

	/*
	 * Since PCI hotplug is not currently supported on pseries, put PCI
	 * hotplug events on the ras_log_buf to be handled by rtas_errd.
	 */
	if (hp_elog->resource == PSERIES_HP_ELOG_RESOURCE_MEM ||
	    hp_elog->resource == PSERIES_HP_ELOG_RESOURCE_CPU ||
	    hp_elog->resource == PSERIES_HP_ELOG_RESOURCE_PMEM)
		queue_hotplug_event(hp_elog);
	else
		log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, 0);

	spin_unlock(&ras_log_buf_lock);
	return IRQ_HANDLED;
}

/* Handle environmental and power warning (EPOW) interrupts. */
static irqreturn_t ras_epow_interrupt(int irq, void *dev_id)
{
	int status;
	int state;
	int critical;

	status = rtas_get_sensor_fast(EPOW_SENSOR_TOKEN, EPOW_SENSOR_INDEX,
				      &state);

	if (state > 3)
		critical = 1;		/* Time Critical */
	else
		critical = 0;

	spin_lock(&ras_log_buf_lock);

	status = rtas_call(ras_check_exception_token, 6, 1, NULL,
			   RTAS_VECTOR_EXTERNAL_INTERRUPT,
			   virq_to_hw(irq),
			   RTAS_EPOW_WARNING,
			   critical, __pa(&ras_log_buf),
				rtas_get_error_log_max());

	log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, 0);

	rtas_parse_epow_errlog((struct rtas_error_log *)ras_log_buf);

	spin_unlock(&ras_log_buf_lock);
	return IRQ_HANDLED;
}

/*
 * Handle hardware error interrupts.
 *
 * RTAS check-exception is called to collect data on the exception.  If
 * the error is deemed recoverable, we log a warning and return.
 * For nonrecoverable errors, an error is logged and we stop all processing
 * as quickly as possible in order to prevent propagation of the failure.
 */
static irqreturn_t ras_error_interrupt(int irq, void *dev_id)
{
	struct rtas_error_log *rtas_elog;
	int status;
	int fatal;

	spin_lock(&ras_log_buf_lock);

	status = rtas_call(ras_check_exception_token, 6, 1, NULL,
			   RTAS_VECTOR_EXTERNAL_INTERRUPT,
			   virq_to_hw(irq),
			   RTAS_INTERNAL_ERROR, 1 /* Time Critical */,
			   __pa(&ras_log_buf),
				rtas_get_error_log_max());

	rtas_elog = (struct rtas_error_log *)ras_log_buf;

	if (status == 0 &&
	    rtas_error_severity(rtas_elog) >= RTAS_SEVERITY_ERROR_SYNC)
		fatal = 1;
	else
		fatal = 0;

	/* format and print the extended information */
	log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, fatal);

	if (fatal) {
		pr_emerg("Fatal hardware error detected. Check RTAS error"
			 " log for details. Powering off immediately\n");
		emergency_sync();
		kernel_power_off();
	} else {
		pr_err("Recoverable hardware error detected\n");
	}

	spin_unlock(&ras_log_buf_lock);
	return IRQ_HANDLED;
}

/*
 * Some versions of FWNMI place the buffer inside the 4kB page starting at
 * 0x7000. Other versions place it inside the rtas buffer. We check both.
 */
#define VALID_FWNMI_BUFFER(A) \
	((((A) >= 0x7000) && ((A) < 0x7ff0)) || \
	(((A) >= rtas.base) && ((A) < (rtas.base + rtas.size - 16))))

static inline struct rtas_error_log *fwnmi_get_errlog(void)
{
	return (struct rtas_error_log *)local_paca->mce_data_buf;
}

/*
 * Get the error information for errors coming through the
 * FWNMI vectors.  The pt_regs' r3 will be updated to reflect
 * the actual r3 if possible, and a ptr to the error log entry
 * will be returned if found.
 *
 * Use one buffer mce_data_buf per cpu to store RTAS error.
 *
 * The mce_data_buf does not have any locks or protection around it,
 * if a second machine check comes in, or a system reset is done
 * before we have logged the error, then we will get corruption in the
 * error log.  This is preferable over holding off on calling
 * ibm,nmi-interlock which would result in us checkstopping if a
 * second machine check did come in.
 */
static struct rtas_error_log *fwnmi_get_errinfo(struct pt_regs *regs)
{
	unsigned long *savep;
	struct rtas_error_log *h;

	/* Mask top two bits */
	regs->gpr[3] &= ~(0x3UL << 62);

	if (!VALID_FWNMI_BUFFER(regs->gpr[3])) {
		printk(KERN_ERR "FWNMI: corrupt r3 0x%016lx\n", regs->gpr[3]);
		return NULL;
	}

	savep = __va(regs->gpr[3]);
	regs->gpr[3] = be64_to_cpu(savep[0]);	/* restore original r3 */

	h = (struct rtas_error_log *)&savep[1];
	/* Use the per cpu buffer from paca to store rtas error log */
	memset(local_paca->mce_data_buf, 0, RTAS_ERROR_LOG_MAX);
	if (!rtas_error_extended(h)) {
		memcpy(local_paca->mce_data_buf, h, sizeof(__u64));
	} else {
		int len, error_log_length;

		error_log_length = 8 + rtas_error_extended_log_length(h);
		len = min_t(int, error_log_length, RTAS_ERROR_LOG_MAX);
		memcpy(local_paca->mce_data_buf, h, len);
	}

	return (struct rtas_error_log *)local_paca->mce_data_buf;
}

/* Call this when done with the data returned by FWNMI_get_errinfo.
 * It will release the saved data area for other CPUs in the
 * partition to receive FWNMI errors.
 */
static void fwnmi_release_errinfo(void)
{
	int ret = rtas_call(rtas_token("ibm,nmi-interlock"), 0, 1, NULL);
	if (ret != 0)
		printk(KERN_ERR "FWNMI: nmi-interlock failed: %d\n", ret);
}

int pSeries_system_reset_exception(struct pt_regs *regs)
{
#ifdef __LITTLE_ENDIAN__
	/*
	 * Some firmware byteswaps SRR registers and gives incorrect SRR1. Try
	 * to detect the bad SRR1 pattern here. Flip the NIP back to correct
	 * endian for reporting purposes. Unfortunately the MSR can't be fixed,
	 * so clear it. It will be missing MSR_RI so we won't try to recover.
	 */
	if ((be64_to_cpu(regs->msr) &
			(MSR_LE|MSR_RI|MSR_DR|MSR_IR|MSR_ME|MSR_PR|
			 MSR_ILE|MSR_HV|MSR_SF)) == (MSR_DR|MSR_SF)) {
		regs->nip = be64_to_cpu((__be64)regs->nip);
		regs->msr = 0;
	}
#endif

	if (fwnmi_active) {
		struct rtas_error_log *errhdr = fwnmi_get_errinfo(regs);
		if (errhdr) {
			/* XXX Should look at FWNMI information */
		}
		fwnmi_release_errinfo();
	}

	if (smp_handle_nmi_ipi(regs))
		return 1;

	return 0; /* need to perform reset */
}


static int mce_handle_error(struct pt_regs *regs, struct rtas_error_log *errp)
{
	struct mce_error_info mce_err = { 0 };
	unsigned long eaddr = 0, paddr = 0;
	struct pseries_errorlog *pseries_log;
	struct pseries_mc_errorlog *mce_log;
	int disposition = rtas_error_disposition(errp);
	int initiator = rtas_error_initiator(errp);
	int severity = rtas_error_severity(errp);
	u8 error_type, err_sub_type;

	if (initiator == RTAS_INITIATOR_UNKNOWN)
		mce_err.initiator = MCE_INITIATOR_UNKNOWN;
	else if (initiator == RTAS_INITIATOR_CPU)
		mce_err.initiator = MCE_INITIATOR_CPU;
	else if (initiator == RTAS_INITIATOR_PCI)
		mce_err.initiator = MCE_INITIATOR_PCI;
	else if (initiator == RTAS_INITIATOR_ISA)
		mce_err.initiator = MCE_INITIATOR_ISA;
	else if (initiator == RTAS_INITIATOR_MEMORY)
		mce_err.initiator = MCE_INITIATOR_MEMORY;
	else if (initiator == RTAS_INITIATOR_POWERMGM)
		mce_err.initiator = MCE_INITIATOR_POWERMGM;
	else
		mce_err.initiator = MCE_INITIATOR_UNKNOWN;

	if (severity == RTAS_SEVERITY_NO_ERROR)
		mce_err.severity = MCE_SEV_NO_ERROR;
	else if (severity == RTAS_SEVERITY_EVENT)
		mce_err.severity = MCE_SEV_WARNING;
	else if (severity == RTAS_SEVERITY_WARNING)
		mce_err.severity = MCE_SEV_WARNING;
	else if (severity == RTAS_SEVERITY_ERROR_SYNC)
		mce_err.severity = MCE_SEV_SEVERE;
	else if (severity == RTAS_SEVERITY_ERROR)
		mce_err.severity = MCE_SEV_SEVERE;
	else if (severity == RTAS_SEVERITY_FATAL)
		mce_err.severity = MCE_SEV_FATAL;
	else
		mce_err.severity = MCE_SEV_FATAL;

	if (severity <= RTAS_SEVERITY_ERROR_SYNC)
		mce_err.sync_error = true;
	else
		mce_err.sync_error = false;

	mce_err.error_type = MCE_ERROR_TYPE_UNKNOWN;
	mce_err.error_class = MCE_ECLASS_UNKNOWN;

	if (!rtas_error_extended(errp))
		goto out;

	pseries_log = get_pseries_errorlog(errp, PSERIES_ELOG_SECT_ID_MCE);
	if (pseries_log == NULL)
		goto out;

	mce_log = (struct pseries_mc_errorlog *)pseries_log->data;
	error_type = mce_log->error_type;
	err_sub_type = rtas_mc_error_sub_type(mce_log);

	switch (mce_log->error_type) {
	case MC_ERROR_TYPE_UE:
		mce_err.error_type = MCE_ERROR_TYPE_UE;
		mce_common_process_ue(regs, &mce_err);
		if (mce_err.ignore_event)
			disposition = RTAS_DISP_FULLY_RECOVERED;
		switch (err_sub_type) {
		case MC_ERROR_UE_IFETCH:
			mce_err.u.ue_error_type = MCE_UE_ERROR_IFETCH;
			break;
		case MC_ERROR_UE_PAGE_TABLE_WALK_IFETCH:
			mce_err.u.ue_error_type = MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH;
			break;
		case MC_ERROR_UE_LOAD_STORE:
			mce_err.u.ue_error_type = MCE_UE_ERROR_LOAD_STORE;
			break;
		case MC_ERROR_UE_PAGE_TABLE_WALK_LOAD_STORE:
			mce_err.u.ue_error_type = MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE;
			break;
		case MC_ERROR_UE_INDETERMINATE:
		default:
			mce_err.u.ue_error_type = MCE_UE_ERROR_INDETERMINATE;
			break;
		}
		if (mce_log->sub_err_type & UE_EFFECTIVE_ADDR_PROVIDED)
			eaddr = be64_to_cpu(mce_log->effective_address);

		if (mce_log->sub_err_type & UE_LOGICAL_ADDR_PROVIDED) {
			paddr = be64_to_cpu(mce_log->logical_address);
		} else if (mce_log->sub_err_type & UE_EFFECTIVE_ADDR_PROVIDED) {
			unsigned long pfn;

			pfn = addr_to_pfn(regs, eaddr);
			if (pfn != ULONG_MAX)
				paddr = pfn << PAGE_SHIFT;
		}

		break;
	case MC_ERROR_TYPE_SLB:
		mce_err.error_type = MCE_ERROR_TYPE_SLB;
		switch (err_sub_type) {
		case MC_ERROR_SLB_PARITY:
			mce_err.u.slb_error_type = MCE_SLB_ERROR_PARITY;
			break;
		case MC_ERROR_SLB_MULTIHIT:
			mce_err.u.slb_error_type = MCE_SLB_ERROR_MULTIHIT;
			break;
		case MC_ERROR_SLB_INDETERMINATE:
		default:
			mce_err.u.slb_error_type = MCE_SLB_ERROR_INDETERMINATE;
			break;
		}
		if (mce_log->sub_err_type & 0x80)
			eaddr = be64_to_cpu(mce_log->effective_address);
		break;
	case MC_ERROR_TYPE_ERAT:
		mce_err.error_type = MCE_ERROR_TYPE_ERAT;
		switch (err_sub_type) {
		case MC_ERROR_ERAT_PARITY:
			mce_err.u.erat_error_type = MCE_ERAT_ERROR_PARITY;
			break;
		case MC_ERROR_ERAT_MULTIHIT:
			mce_err.u.erat_error_type = MCE_ERAT_ERROR_MULTIHIT;
			break;
		case MC_ERROR_ERAT_INDETERMINATE:
		default:
			mce_err.u.erat_error_type = MCE_ERAT_ERROR_INDETERMINATE;
			break;
		}
		if (mce_log->sub_err_type & 0x80)
			eaddr = be64_to_cpu(mce_log->effective_address);
		break;
	case MC_ERROR_TYPE_TLB:
		mce_err.error_type = MCE_ERROR_TYPE_TLB;
		switch (err_sub_type) {
		case MC_ERROR_TLB_PARITY:
			mce_err.u.tlb_error_type = MCE_TLB_ERROR_PARITY;
			break;
		case MC_ERROR_TLB_MULTIHIT:
			mce_err.u.tlb_error_type = MCE_TLB_ERROR_MULTIHIT;
			break;
		case MC_ERROR_TLB_INDETERMINATE:
		default:
			mce_err.u.tlb_error_type = MCE_TLB_ERROR_INDETERMINATE;
			break;
		}
		if (mce_log->sub_err_type & 0x80)
			eaddr = be64_to_cpu(mce_log->effective_address);
		break;
	case MC_ERROR_TYPE_D_CACHE:
		mce_err.error_type = MCE_ERROR_TYPE_DCACHE;
		break;
	case MC_ERROR_TYPE_I_CACHE:
		mce_err.error_type = MCE_ERROR_TYPE_DCACHE;
		break;
	case MC_ERROR_TYPE_UNKNOWN:
	default:
		mce_err.error_type = MCE_ERROR_TYPE_UNKNOWN;
		break;
	}

#ifdef CONFIG_PPC_BOOK3S_64
	if (disposition == RTAS_DISP_NOT_RECOVERED) {
		switch (error_type) {
		case	MC_ERROR_TYPE_SLB:
		case	MC_ERROR_TYPE_ERAT:
			/*
			 * Store the old slb content in paca before flushing.
			 * Print this when we go to virtual mode.
			 * There are chances that we may hit MCE again if there
			 * is a parity error on the SLB entry we trying to read
			 * for saving. Hence limit the slb saving to single
			 * level of recursion.
			 */
			if (local_paca->in_mce == 1)
				slb_save_contents(local_paca->mce_faulty_slbs);
			flush_and_reload_slb();
			disposition = RTAS_DISP_FULLY_RECOVERED;
			break;
		default:
			break;
		}
	} else if (disposition == RTAS_DISP_LIMITED_RECOVERY) {
		/* Platform corrected itself but could be degraded */
		printk(KERN_ERR "MCE: limited recovery, system may "
		       "be degraded\n");
		disposition = RTAS_DISP_FULLY_RECOVERED;
	}
#endif

out:
	/*
	 * Enable translation as we will be accessing per-cpu variables
	 * in save_mce_event() which may fall outside RMO region, also
	 * leave it enabled because subsequently we will be queuing work
	 * to workqueues where again per-cpu variables accessed, besides
	 * fwnmi_release_errinfo() crashes when called in realmode on
	 * pseries.
	 * Note: All the realmode handling like flushing SLB entries for
	 *       SLB multihit is done by now.
	 */
	mtmsr(mfmsr() | MSR_IR | MSR_DR);
	save_mce_event(regs, disposition == RTAS_DISP_FULLY_RECOVERED,
			&mce_err, regs->nip, eaddr, paddr);

	return disposition;
}

/*
 * Process MCE rtas errlog event.
 */
static void mce_process_errlog_event(struct irq_work *work)
{
	struct rtas_error_log *err;

	err = fwnmi_get_errlog();
	log_error((char *)err, ERR_TYPE_RTAS_LOG, 0);
}

/*
 * See if we can recover from a machine check exception.
 * This is only called on power4 (or above) and only via
 * the Firmware Non-Maskable Interrupts (fwnmi) handler
 * which provides the error analysis for us.
 *
 * Return 1 if corrected (or delivered a signal).
 * Return 0 if there is nothing we can do.
 */
static int recover_mce(struct pt_regs *regs, struct machine_check_event *evt)
{
	int recovered = 0;

	if (!(regs->msr & MSR_RI)) {
		/* If MSR_RI isn't set, we cannot recover */
		pr_err("Machine check interrupt unrecoverable: MSR(RI=0)\n");
		recovered = 0;
	} else if (evt->disposition == MCE_DISPOSITION_RECOVERED) {
		/* Platform corrected itself */
		recovered = 1;
	} else if (evt->severity == MCE_SEV_FATAL) {
		/* Fatal machine check */
		pr_err("Machine check interrupt is fatal\n");
		recovered = 0;
	}

	if (!recovered && evt->sync_error) {
		/*
		 * Try to kill processes if we get a synchronous machine check
		 * (e.g., one caused by execution of this instruction). This
		 * will devolve into a panic if we try to kill init or are in
		 * an interrupt etc.
		 *
		 * TODO: Queue up this address for hwpoisioning later.
		 * TODO: This is not quite right for d-side machine
		 *       checks ->nip is not necessarily the important
		 *       address.
		 */
		if ((user_mode(regs))) {
			_exception(SIGBUS, regs, BUS_MCEERR_AR, regs->nip);
			recovered = 1;
		} else if (die_will_crash()) {
			/*
			 * die() would kill the kernel, so better to go via
			 * the platform reboot code that will log the
			 * machine check.
			 */
			recovered = 0;
		} else {
			die("Machine check", regs, SIGBUS);
			recovered = 1;
		}
	}

	return recovered;
}

/*
 * Handle a machine check.
 *
 * Note that on Power 4 and beyond Firmware Non-Maskable Interrupts (fwnmi)
 * should be present.  If so the handler which called us tells us if the
 * error was recovered (never true if RI=0).
 *
 * On hardware prior to Power 4 these exceptions were asynchronous which
 * means we can't tell exactly where it occurred and so we can't recover.
 */
int pSeries_machine_check_exception(struct pt_regs *regs)
{
	struct machine_check_event evt;

	if (!get_mce_event(&evt, MCE_EVENT_RELEASE))
		return 0;

	/* Print things out */
	if (evt.version != MCE_V1) {
		pr_err("Machine Check Exception, Unknown event version %d !\n",
		       evt.version);
		return 0;
	}
	machine_check_print_event_info(&evt, user_mode(regs), false);

	if (recover_mce(regs, &evt))
		return 1;

	return 0;
}

long pseries_machine_check_realmode(struct pt_regs *regs)
{
	struct rtas_error_log *errp;
	int disposition;

	if (fwnmi_active) {
		errp = fwnmi_get_errinfo(regs);
		/*
		 * Call to fwnmi_release_errinfo() in real mode causes kernel
		 * to panic. Hence we will call it as soon as we go into
		 * virtual mode.
		 */
		disposition = mce_handle_error(regs, errp);
		fwnmi_release_errinfo();

		/* Queue irq work to log this rtas event later. */
		irq_work_queue(&mce_errlog_process_work);

		if (disposition == RTAS_DISP_FULLY_RECOVERED)
			return 1;
	}

	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Implement the AER root port service driver. The driver registers an IRQ
 * handler. When a root port triggers an AER interrupt, the IRQ handler
 * collects root port status and schedules work.
 *
 * Copyright (C) 2006 Intel Corp.
 *	Tom Long Nguyen (tom.l.nguyen@intel.com)
 *	Zhang Yanmin (yanmin.zhang@intel.com)
 *
 * (C) Copyright 2009 Hewlett-Packard Development Company, L.P.
 *    Andrew Patterson <andrew.patterson@hp.com>
 */

#define pr_fmt(fmt) "AER: " fmt
#define dev_fmt pr_fmt

#include <linux/bitops.h>
#include <linux/cper.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/slab.h>
#include <acpi/apei.h>
#include <ras/ras_event.h>

#include "../pci.h"
#include "portdrv.h"

#define AER_ERROR_SOURCES_MAX		128

#define AER_MAX_TYPEOF_COR_ERRS		16	/* as per PCI_ERR_COR_STATUS */
#define AER_MAX_TYPEOF_UNCOR_ERRS	27	/* as per PCI_ERR_UNCOR_STATUS*/

struct aer_err_source {
	unsigned int status;
	unsigned int id;
};

struct aer_rpc {
	struct pci_dev *rpd;		/* Root Port device */
	DECLARE_KFIFO(aer_fifo, struct aer_err_source, AER_ERROR_SOURCES_MAX);
};

/* AER stats for the device */
struct aer_stats {

	/*
	 * Fields for all AER capable devices. They indicate the errors
	 * "as seen by this device". Note that this may mean that if an
	 * end point is causing problems, the AER counters may increment
	 * at its link partner (e.g. root port) because the errors will be
	 * "seen" by the link partner and not the the problematic end point
	 * itself (which may report all counters as 0 as it never saw any
	 * problems).
	 */
	/* Counters for different type of correctable errors */
	u64 dev_cor_errs[AER_MAX_TYPEOF_COR_ERRS];
	/* Counters for different type of fatal uncorrectable errors */
	u64 dev_fatal_errs[AER_MAX_TYPEOF_UNCOR_ERRS];
	/* Counters for different type of nonfatal uncorrectable errors */
	u64 dev_nonfatal_errs[AER_MAX_TYPEOF_UNCOR_ERRS];
	/* Total number of ERR_COR sent by this device */
	u64 dev_total_cor_errs;
	/* Total number of ERR_FATAL sent by this device */
	u64 dev_total_fatal_errs;
	/* Total number of ERR_NONFATAL sent by this device */
	u64 dev_total_nonfatal_errs;

	/*
	 * Fields for Root ports & root complex event collectors only, these
	 * indicate the total number of ERR_COR, ERR_FATAL, and ERR_NONFATAL
	 * messages received by the root port / event collector, INCLUDING the
	 * ones that are generated internally (by the rootport itself)
	 */
	u64 rootport_total_cor_errs;
	u64 rootport_total_fatal_errs;
	u64 rootport_total_nonfatal_errs;
};

#define AER_LOG_TLP_MASKS		(PCI_ERR_UNC_POISON_TLP|	\
					PCI_ERR_UNC_ECRC|		\
					PCI_ERR_UNC_UNSUP|		\
					PCI_ERR_UNC_COMP_ABORT|		\
					PCI_ERR_UNC_UNX_COMP|		\
					PCI_ERR_UNC_MALF_TLP)

#define SYSTEM_ERROR_INTR_ON_MESG_MASK	(PCI_EXP_RTCTL_SECEE|	\
					PCI_EXP_RTCTL_SENFEE|	\
					PCI_EXP_RTCTL_SEFEE)
#define ROOT_PORT_INTR_ON_MESG_MASK	(PCI_ERR_ROOT_CMD_COR_EN|	\
					PCI_ERR_ROOT_CMD_NONFATAL_EN|	\
					PCI_ERR_ROOT_CMD_FATAL_EN)
#define ERR_COR_ID(d)			(d & 0xffff)
#define ERR_UNCOR_ID(d)			(d >> 16)

#define AER_ERR_STATUS_MASK		(PCI_ERR_ROOT_UNCOR_RCV |	\
					PCI_ERR_ROOT_COR_RCV |		\
					PCI_ERR_ROOT_MULTI_COR_RCV |	\
					PCI_ERR_ROOT_MULTI_UNCOR_RCV)

static int pcie_aer_disable;
static pci_ers_result_t aer_root_reset(struct pci_dev *dev);

void pci_no_aer(void)
{
	pcie_aer_disable = 1;
}

bool pci_aer_available(void)
{
	return !pcie_aer_disable && pci_msi_enabled();
}

#ifdef CONFIG_PCIE_ECRC

#define ECRC_POLICY_DEFAULT 0		/* ECRC set by BIOS */
#define ECRC_POLICY_OFF     1		/* ECRC off for performance */
#define ECRC_POLICY_ON      2		/* ECRC on for data integrity */

static int ecrc_policy = ECRC_POLICY_DEFAULT;

static const char * const ecrc_policy_str[] = {
	[ECRC_POLICY_DEFAULT] = "bios",
	[ECRC_POLICY_OFF] = "off",
	[ECRC_POLICY_ON] = "on"
};

/**
 * enable_ercr_checking - enable PCIe ECRC checking for a device
 * @dev: the PCI device
 *
 * Returns 0 on success, or negative on failure.
 */
static int enable_ecrc_checking(struct pci_dev *dev)
{
	int aer = dev->aer_cap;
	u32 reg32;

	if (!aer)
		return -ENODEV;

	pci_read_config_dword(dev, aer + PCI_ERR_CAP, &reg32);
	if (reg32 & PCI_ERR_CAP_ECRC_GENC)
		reg32 |= PCI_ERR_CAP_ECRC_GENE;
	if (reg32 & PCI_ERR_CAP_ECRC_CHKC)
		reg32 |= PCI_ERR_CAP_ECRC_CHKE;
	pci_write_config_dword(dev, aer + PCI_ERR_CAP, reg32);

	return 0;
}

/**
 * disable_ercr_checking - disables PCIe ECRC checking for a device
 * @dev: the PCI device
 *
 * Returns 0 on success, or negative on failure.
 */
static int disable_ecrc_checking(struct pci_dev *dev)
{
	int aer = dev->aer_cap;
	u32 reg32;

	if (!aer)
		return -ENODEV;

	pci_read_config_dword(dev, aer + PCI_ERR_CAP, &reg32);
	reg32 &= ~(PCI_ERR_CAP_ECRC_GENE | PCI_ERR_CAP_ECRC_CHKE);
	pci_write_config_dword(dev, aer + PCI_ERR_CAP, reg32);

	return 0;
}

/**
 * pcie_set_ecrc_checking - set/unset PCIe ECRC checking for a device based on global policy
 * @dev: the PCI device
 */
void pcie_set_ecrc_checking(struct pci_dev *dev)
{
	switch (ecrc_policy) {
	case ECRC_POLICY_DEFAULT:
		return;
	case ECRC_POLICY_OFF:
		disable_ecrc_checking(dev);
		break;
	case ECRC_POLICY_ON:
		enable_ecrc_checking(dev);
		break;
	default:
		return;
	}
}

/**
 * pcie_ecrc_get_policy - parse kernel command-line ecrc option
 * @str: ECRC policy from kernel command line to use
 */
void pcie_ecrc_get_policy(char *str)
{
	int i;

	i = match_string(ecrc_policy_str, ARRAY_SIZE(ecrc_policy_str), str);
	if (i < 0)
		return;

	ecrc_policy = i;
}
#endif	/* CONFIG_PCIE_ECRC */

#define	PCI_EXP_AER_FLAGS	(PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE | \
				 PCI_EXP_DEVCTL_FERE | PCI_EXP_DEVCTL_URRE)

int pcie_aer_is_native(struct pci_dev *dev)
{
	struct pci_host_bridge *host = pci_find_host_bridge(dev->bus);

	if (!dev->aer_cap)
		return 0;

	return pcie_ports_native || host->native_aer;
}

int pci_enable_pcie_error_reporting(struct pci_dev *dev)
{
	int rc;

	if (!pcie_aer_is_native(dev))
		return -EIO;

	rc = pcie_capability_set_word(dev, PCI_EXP_DEVCTL, PCI_EXP_AER_FLAGS);
	return pcibios_err_to_errno(rc);
}
EXPORT_SYMBOL_GPL(pci_enable_pcie_error_reporting);

int pci_disable_pcie_error_reporting(struct pci_dev *dev)
{
	int rc;

	if (!pcie_aer_is_native(dev))
		return -EIO;

	rc = pcie_capability_clear_word(dev, PCI_EXP_DEVCTL, PCI_EXP_AER_FLAGS);
	return pcibios_err_to_errno(rc);
}
EXPORT_SYMBOL_GPL(pci_disable_pcie_error_reporting);

int pci_aer_clear_nonfatal_status(struct pci_dev *dev)
{
	int aer = dev->aer_cap;
	u32 status, sev;

	if (!pcie_aer_is_native(dev))
		return -EIO;

	/* Clear status bits for ERR_NONFATAL errors only */
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_SEVER, &sev);
	status &= ~sev;
	if (status)
		pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, status);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_aer_clear_nonfatal_status);

void pci_aer_clear_fatal_status(struct pci_dev *dev)
{
	int aer = dev->aer_cap;
	u32 status, sev;

	if (!pcie_aer_is_native(dev))
		return;

	/* Clear status bits for ERR_FATAL errors only */
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_SEVER, &sev);
	status &= sev;
	if (status)
		pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, status);
}

/**
 * pci_aer_raw_clear_status - Clear AER error registers.
 * @dev: the PCI device
 *
 * Clearing AER error status registers unconditionally, regardless of
 * whether they're owned by firmware or the OS.
 *
 * Returns 0 on success, or negative on failure.
 */
int pci_aer_raw_clear_status(struct pci_dev *dev)
{
	int aer = dev->aer_cap;
	u32 status;
	int port_type;

	if (!aer)
		return -EIO;

	port_type = pci_pcie_type(dev);
	if (port_type == PCI_EXP_TYPE_ROOT_PORT ||
	    port_type == PCI_EXP_TYPE_RC_EC) {
		pci_read_config_dword(dev, aer + PCI_ERR_ROOT_STATUS, &status);
		pci_write_config_dword(dev, aer + PCI_ERR_ROOT_STATUS, status);
	}

	pci_read_config_dword(dev, aer + PCI_ERR_COR_STATUS, &status);
	pci_write_config_dword(dev, aer + PCI_ERR_COR_STATUS, status);

	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, &status);
	pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, status);

	return 0;
}

int pci_aer_clear_status(struct pci_dev *dev)
{
	if (!pcie_aer_is_native(dev))
		return -EIO;

	return pci_aer_raw_clear_status(dev);
}

void pci_save_aer_state(struct pci_dev *dev)
{
	int aer = dev->aer_cap;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!aer)
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_ERR);
	if (!save_state)
		return;

	cap = &save_state->cap.data[0];
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_MASK, cap++);
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_SEVER, cap++);
	pci_read_config_dword(dev, aer + PCI_ERR_COR_MASK, cap++);
	pci_read_config_dword(dev, aer + PCI_ERR_CAP, cap++);
	if (pcie_cap_has_rtctl(dev))
		pci_read_config_dword(dev, aer + PCI_ERR_ROOT_COMMAND, cap++);
}

void pci_restore_aer_state(struct pci_dev *dev)
{
	int aer = dev->aer_cap;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!aer)
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_ERR);
	if (!save_state)
		return;

	cap = &save_state->cap.data[0];
	pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_MASK, *cap++);
	pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_SEVER, *cap++);
	pci_write_config_dword(dev, aer + PCI_ERR_COR_MASK, *cap++);
	pci_write_config_dword(dev, aer + PCI_ERR_CAP, *cap++);
	if (pcie_cap_has_rtctl(dev))
		pci_write_config_dword(dev, aer + PCI_ERR_ROOT_COMMAND, *cap++);
}

void pci_aer_init(struct pci_dev *dev)
{
	int n;

	dev->aer_cap = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!dev->aer_cap)
		return;

	dev->aer_stats = kzalloc(sizeof(struct aer_stats), GFP_KERNEL);

	/*
	 * We save/restore PCI_ERR_UNCOR_MASK, PCI_ERR_UNCOR_SEVER,
	 * PCI_ERR_COR_MASK, and PCI_ERR_CAP.  Root and Root Complex Event
	 * Collectors also implement PCI_ERR_ROOT_COMMAND (PCIe r5.0, sec
	 * 7.8.4).
	 */
	n = pcie_cap_has_rtctl(dev) ? 5 : 4;
	pci_add_ext_cap_save_buffer(dev, PCI_EXT_CAP_ID_ERR, sizeof(u32) * n);

	pci_aer_clear_status(dev);
}

void pci_aer_exit(struct pci_dev *dev)
{
	kfree(dev->aer_stats);
	dev->aer_stats = NULL;
}

#define AER_AGENT_RECEIVER		0
#define AER_AGENT_REQUESTER		1
#define AER_AGENT_COMPLETER		2
#define AER_AGENT_TRANSMITTER		3

#define AER_AGENT_REQUESTER_MASK(t)	((t == AER_CORRECTABLE) ?	\
	0 : (PCI_ERR_UNC_COMP_TIME|PCI_ERR_UNC_UNSUP))
#define AER_AGENT_COMPLETER_MASK(t)	((t == AER_CORRECTABLE) ?	\
	0 : PCI_ERR_UNC_COMP_ABORT)
#define AER_AGENT_TRANSMITTER_MASK(t)	((t == AER_CORRECTABLE) ?	\
	(PCI_ERR_COR_REP_ROLL|PCI_ERR_COR_REP_TIMER) : 0)

#define AER_GET_AGENT(t, e)						\
	((e & AER_AGENT_COMPLETER_MASK(t)) ? AER_AGENT_COMPLETER :	\
	(e & AER_AGENT_REQUESTER_MASK(t)) ? AER_AGENT_REQUESTER :	\
	(e & AER_AGENT_TRANSMITTER_MASK(t)) ? AER_AGENT_TRANSMITTER :	\
	AER_AGENT_RECEIVER)

#define AER_PHYSICAL_LAYER_ERROR	0
#define AER_DATA_LINK_LAYER_ERROR	1
#define AER_TRANSACTION_LAYER_ERROR	2

#define AER_PHYSICAL_LAYER_ERROR_MASK(t) ((t == AER_CORRECTABLE) ?	\
	PCI_ERR_COR_RCVR : 0)
#define AER_DATA_LINK_LAYER_ERROR_MASK(t) ((t == AER_CORRECTABLE) ?	\
	(PCI_ERR_COR_BAD_TLP|						\
	PCI_ERR_COR_BAD_DLLP|						\
	PCI_ERR_COR_REP_ROLL|						\
	PCI_ERR_COR_REP_TIMER) : PCI_ERR_UNC_DLP)

#define AER_GET_LAYER_ERROR(t, e)					\
	((e & AER_PHYSICAL_LAYER_ERROR_MASK(t)) ? AER_PHYSICAL_LAYER_ERROR : \
	(e & AER_DATA_LINK_LAYER_ERROR_MASK(t)) ? AER_DATA_LINK_LAYER_ERROR : \
	AER_TRANSACTION_LAYER_ERROR)

/*
 * AER error strings
 */
static const char *aer_error_severity_string[] = {
	"Uncorrected (Non-Fatal)",
	"Uncorrected (Fatal)",
	"Corrected"
};

static const char *aer_error_layer[] = {
	"Physical Layer",
	"Data Link Layer",
	"Transaction Layer"
};

static const char *aer_correctable_error_string[] = {
	"RxErr",			/* Bit Position 0	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"BadTLP",			/* Bit Position 6	*/
	"BadDLLP",			/* Bit Position 7	*/
	"Rollover",			/* Bit Position 8	*/
	NULL,
	NULL,
	NULL,
	"Timeout",			/* Bit Position 12	*/
	"NonFatalErr",			/* Bit Position 13	*/
	"CorrIntErr",			/* Bit Position 14	*/
	"HeaderOF",			/* Bit Position 15	*/
	NULL,				/* Bit Position 16	*/
	NULL,				/* Bit Position 17	*/
	NULL,				/* Bit Position 18	*/
	NULL,				/* Bit Position 19	*/
	NULL,				/* Bit Position 20	*/
	NULL,				/* Bit Position 21	*/
	NULL,				/* Bit Position 22	*/
	NULL,				/* Bit Position 23	*/
	NULL,				/* Bit Position 24	*/
	NULL,				/* Bit Position 25	*/
	NULL,				/* Bit Position 26	*/
	NULL,				/* Bit Position 27	*/
	NULL,				/* Bit Position 28	*/
	NULL,				/* Bit Position 29	*/
	NULL,				/* Bit Position 30	*/
	NULL,				/* Bit Position 31	*/
};

static const char *aer_uncorrectable_error_string[] = {
	"Undefined",			/* Bit Position 0	*/
	NULL,
	NULL,
	NULL,
	"DLP",				/* Bit Position 4	*/
	"SDES",				/* Bit Position 5	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"TLP",				/* Bit Position 12	*/
	"FCP",				/* Bit Position 13	*/
	"CmpltTO",			/* Bit Position 14	*/
	"CmpltAbrt",			/* Bit Position 15	*/
	"UnxCmplt",			/* Bit Position 16	*/
	"RxOF",				/* Bit Position 17	*/
	"MalfTLP",			/* Bit Position 18	*/
	"ECRC",				/* Bit Position 19	*/
	"UnsupReq",			/* Bit Position 20	*/
	"ACSViol",			/* Bit Position 21	*/
	"UncorrIntErr",			/* Bit Position 22	*/
	"BlockedTLP",			/* Bit Position 23	*/
	"AtomicOpBlocked",		/* Bit Position 24	*/
	"TLPBlockedErr",		/* Bit Position 25	*/
	"PoisonTLPBlocked",		/* Bit Position 26	*/
	NULL,				/* Bit Position 27	*/
	NULL,				/* Bit Position 28	*/
	NULL,				/* Bit Position 29	*/
	NULL,				/* Bit Position 30	*/
	NULL,				/* Bit Position 31	*/
};

static const char *aer_agent_string[] = {
	"Receiver ID",
	"Requester ID",
	"Completer ID",
	"Transmitter ID"
};

#define aer_stats_dev_attr(name, stats_array, strings_array,		\
			   total_string, total_field)			\
	static ssize_t							\
	name##_show(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	unsigned int i;							\
	char *str = buf;						\
	struct pci_dev *pdev = to_pci_dev(dev);				\
	u64 *stats = pdev->aer_stats->stats_array;			\
									\
	for (i = 0; i < ARRAY_SIZE(strings_array); i++) {		\
		if (strings_array[i])					\
			str += sprintf(str, "%s %llu\n",		\
				       strings_array[i], stats[i]);	\
		else if (stats[i])					\
			str += sprintf(str, #stats_array "_bit[%d] %llu\n",\
				       i, stats[i]);			\
	}								\
	str += sprintf(str, "TOTAL_%s %llu\n", total_string,		\
		       pdev->aer_stats->total_field);			\
	return str-buf;							\
}									\
static DEVICE_ATTR_RO(name)

aer_stats_dev_attr(aer_dev_correctable, dev_cor_errs,
		   aer_correctable_error_string, "ERR_COR",
		   dev_total_cor_errs);
aer_stats_dev_attr(aer_dev_fatal, dev_fatal_errs,
		   aer_uncorrectable_error_string, "ERR_FATAL",
		   dev_total_fatal_errs);
aer_stats_dev_attr(aer_dev_nonfatal, dev_nonfatal_errs,
		   aer_uncorrectable_error_string, "ERR_NONFATAL",
		   dev_total_nonfatal_errs);

#define aer_stats_rootport_attr(name, field)				\
	static ssize_t							\
	name##_show(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	struct pci_dev *pdev = to_pci_dev(dev);				\
	return sprintf(buf, "%llu\n", pdev->aer_stats->field);		\
}									\
static DEVICE_ATTR_RO(name)

aer_stats_rootport_attr(aer_rootport_total_err_cor,
			 rootport_total_cor_errs);
aer_stats_rootport_attr(aer_rootport_total_err_fatal,
			 rootport_total_fatal_errs);
aer_stats_rootport_attr(aer_rootport_total_err_nonfatal,
			 rootport_total_nonfatal_errs);

static struct attribute *aer_stats_attrs[] __ro_after_init = {
	&dev_attr_aer_dev_correctable.attr,
	&dev_attr_aer_dev_fatal.attr,
	&dev_attr_aer_dev_nonfatal.attr,
	&dev_attr_aer_rootport_total_err_cor.attr,
	&dev_attr_aer_rootport_total_err_fatal.attr,
	&dev_attr_aer_rootport_total_err_nonfatal.attr,
	NULL
};

static umode_t aer_stats_attrs_are_visible(struct kobject *kobj,
					   struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (!pdev->aer_stats)
		return 0;

	if ((a == &dev_attr_aer_rootport_total_err_cor.attr ||
	     a == &dev_attr_aer_rootport_total_err_fatal.attr ||
	     a == &dev_attr_aer_rootport_total_err_nonfatal.attr) &&
	    ((pci_pcie_type(pdev) != PCI_EXP_TYPE_ROOT_PORT) &&
	     (pci_pcie_type(pdev) != PCI_EXP_TYPE_RC_EC)))
		return 0;

	return a->mode;
}

const struct attribute_group aer_stats_attr_group = {
	.attrs  = aer_stats_attrs,
	.is_visible = aer_stats_attrs_are_visible,
};

static void pci_dev_aer_stats_incr(struct pci_dev *pdev,
				   struct aer_err_info *info)
{
	unsigned long status = info->status & ~info->mask;
	int i, max = -1;
	u64 *counter = NULL;
	struct aer_stats *aer_stats = pdev->aer_stats;

	if (!aer_stats)
		return;

	switch (info->severity) {
	case AER_CORRECTABLE:
		aer_stats->dev_total_cor_errs++;
		counter = &aer_stats->dev_cor_errs[0];
		max = AER_MAX_TYPEOF_COR_ERRS;
		break;
	case AER_NONFATAL:
		aer_stats->dev_total_nonfatal_errs++;
		counter = &aer_stats->dev_nonfatal_errs[0];
		max = AER_MAX_TYPEOF_UNCOR_ERRS;
		break;
	case AER_FATAL:
		aer_stats->dev_total_fatal_errs++;
		counter = &aer_stats->dev_fatal_errs[0];
		max = AER_MAX_TYPEOF_UNCOR_ERRS;
		break;
	}

	for_each_set_bit(i, &status, max)
		counter[i]++;
}

static void pci_rootport_aer_stats_incr(struct pci_dev *pdev,
				 struct aer_err_source *e_src)
{
	struct aer_stats *aer_stats = pdev->aer_stats;

	if (!aer_stats)
		return;

	if (e_src->status & PCI_ERR_ROOT_COR_RCV)
		aer_stats->rootport_total_cor_errs++;

	if (e_src->status & PCI_ERR_ROOT_UNCOR_RCV) {
		if (e_src->status & PCI_ERR_ROOT_FATAL_RCV)
			aer_stats->rootport_total_fatal_errs++;
		else
			aer_stats->rootport_total_nonfatal_errs++;
	}
}

static void __print_tlp_header(struct pci_dev *dev,
			       struct aer_header_log_regs *t)
{
	pci_err(dev, "  TLP Header: %08x %08x %08x %08x\n",
		t->dw0, t->dw1, t->dw2, t->dw3);
}

static void __aer_print_error(struct pci_dev *dev,
			      struct aer_err_info *info)
{
	const char **strings;
	unsigned long status = info->status & ~info->mask;
	const char *level, *errmsg;
	int i;

	if (info->severity == AER_CORRECTABLE) {
		strings = aer_correctable_error_string;
		level = KERN_WARNING;
	} else {
		strings = aer_uncorrectable_error_string;
		level = KERN_ERR;
	}

	for_each_set_bit(i, &status, 32) {
		errmsg = strings[i];
		if (!errmsg)
			errmsg = "Unknown Error Bit";

		pci_printk(level, dev, "   [%2d] %-22s%s\n", i, errmsg,
				info->first_error == i ? " (First)" : "");
	}
	pci_dev_aer_stats_incr(dev, info);
}

void aer_print_error(struct pci_dev *dev, struct aer_err_info *info)
{
	int layer, agent;
	int id = ((dev->bus->number << 8) | dev->devfn);
	const char *level;

	if (!info->status) {
		pci_err(dev, "PCIe Bus Error: severity=%s, type=Inaccessible, (Unregistered Agent ID)\n",
			aer_error_severity_string[info->severity]);
		goto out;
	}

	layer = AER_GET_LAYER_ERROR(info->severity, info->status);
	agent = AER_GET_AGENT(info->severity, info->status);

	level = (info->severity == AER_CORRECTABLE) ? KERN_WARNING : KERN_ERR;

	pci_printk(level, dev, "PCIe Bus Error: severity=%s, type=%s, (%s)\n",
		   aer_error_severity_string[info->severity],
		   aer_error_layer[layer], aer_agent_string[agent]);

	pci_printk(level, dev, "  device [%04x:%04x] error status/mask=%08x/%08x\n",
		   dev->vendor, dev->device, info->status, info->mask);

	__aer_print_error(dev, info);

	if (info->tlp_header_valid)
		__print_tlp_header(dev, &info->tlp);

out:
	if (info->id && info->error_dev_num > 1 && info->id == id)
		pci_err(dev, "  Error of this Agent is reported first\n");

	trace_aer_event(dev_name(&dev->dev), (info->status & ~info->mask),
			info->severity, info->tlp_header_valid, &info->tlp);
}

static void aer_print_port_info(struct pci_dev *dev, struct aer_err_info *info)
{
	u8 bus = info->id >> 8;
	u8 devfn = info->id & 0xff;

	pci_info(dev, "%s%s error received: %04x:%02x:%02x.%d\n",
		 info->multi_error_valid ? "Multiple " : "",
		 aer_error_severity_string[info->severity],
		 pci_domain_nr(dev->bus), bus, PCI_SLOT(devfn),
		 PCI_FUNC(devfn));
}

#ifdef CONFIG_ACPI_APEI_PCIEAER
int cper_severity_to_aer(int cper_severity)
{
	switch (cper_severity) {
	case CPER_SEV_RECOVERABLE:
		return AER_NONFATAL;
	case CPER_SEV_FATAL:
		return AER_FATAL;
	default:
		return AER_CORRECTABLE;
	}
}
EXPORT_SYMBOL_GPL(cper_severity_to_aer);

void cper_print_aer(struct pci_dev *dev, int aer_severity,
		    struct aer_capability_regs *aer)
{
	int layer, agent, tlp_header_valid = 0;
	u32 status, mask;
	struct aer_err_info info;

	if (aer_severity == AER_CORRECTABLE) {
		status = aer->cor_status;
		mask = aer->cor_mask;
	} else {
		status = aer->uncor_status;
		mask = aer->uncor_mask;
		tlp_header_valid = status & AER_LOG_TLP_MASKS;
	}

	layer = AER_GET_LAYER_ERROR(aer_severity, status);
	agent = AER_GET_AGENT(aer_severity, status);

	memset(&info, 0, sizeof(info));
	info.severity = aer_severity;
	info.status = status;
	info.mask = mask;
	info.first_error = PCI_ERR_CAP_FEP(aer->cap_control);

	pci_err(dev, "aer_status: 0x%08x, aer_mask: 0x%08x\n", status, mask);
	__aer_print_error(dev, &info);
	pci_err(dev, "aer_layer=%s, aer_agent=%s\n",
		aer_error_layer[layer], aer_agent_string[agent]);

	if (aer_severity != AER_CORRECTABLE)
		pci_err(dev, "aer_uncor_severity: 0x%08x\n",
			aer->uncor_severity);

	if (tlp_header_valid)
		__print_tlp_header(dev, &aer->header_log);

	trace_aer_event(dev_name(&dev->dev), (status & ~mask),
			aer_severity, tlp_header_valid, &aer->header_log);
}
#endif

/**
 * add_error_device - list device to be handled
 * @e_info: pointer to error info
 * @dev: pointer to pci_dev to be added
 */
static int add_error_device(struct aer_err_info *e_info, struct pci_dev *dev)
{
	if (e_info->error_dev_num < AER_MAX_MULTI_ERR_DEVICES) {
		e_info->dev[e_info->error_dev_num] = pci_dev_get(dev);
		e_info->error_dev_num++;
		return 0;
	}
	return -ENOSPC;
}

/**
 * is_error_source - check whether the device is source of reported error
 * @dev: pointer to pci_dev to be checked
 * @e_info: pointer to reported error info
 */
static bool is_error_source(struct pci_dev *dev, struct aer_err_info *e_info)
{
	int aer = dev->aer_cap;
	u32 status, mask;
	u16 reg16;

	/*
	 * When bus id is equal to 0, it might be a bad id
	 * reported by root port.
	 */
	if ((PCI_BUS_NUM(e_info->id) != 0) &&
	    !(dev->bus->bus_flags & PCI_BUS_FLAGS_NO_AERSID)) {
		/* Device ID match? */
		if (e_info->id == ((dev->bus->number << 8) | dev->devfn))
			return true;

		/* Continue id comparing if there is no multiple error */
		if (!e_info->multi_error_valid)
			return false;
	}

	/*
	 * When either
	 *      1) bus id is equal to 0. Some ports might lose the bus
	 *              id of error source id;
	 *      2) bus flag PCI_BUS_FLAGS_NO_AERSID is set
	 *      3) There are multiple errors and prior ID comparing fails;
	 * We check AER status registers to find possible reporter.
	 */
	if (atomic_read(&dev->enable_cnt) == 0)
		return false;

	/* Check if AER is enabled */
	pcie_capability_read_word(dev, PCI_EXP_DEVCTL, &reg16);
	if (!(reg16 & PCI_EXP_AER_FLAGS))
		return false;

	if (!aer)
		return false;

	/* Check if error is recorded */
	if (e_info->severity == AER_CORRECTABLE) {
		pci_read_config_dword(dev, aer + PCI_ERR_COR_STATUS, &status);
		pci_read_config_dword(dev, aer + PCI_ERR_COR_MASK, &mask);
	} else {
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, &status);
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_MASK, &mask);
	}
	if (status & ~mask)
		return true;

	return false;
}

static int find_device_iter(struct pci_dev *dev, void *data)
{
	struct aer_err_info *e_info = (struct aer_err_info *)data;

	if (is_error_source(dev, e_info)) {
		/* List this device */
		if (add_error_device(e_info, dev)) {
			/* We cannot handle more... Stop iteration */
			/* TODO: Should print error message here? */
			return 1;
		}

		/* If there is only a single error, stop iteration */
		if (!e_info->multi_error_valid)
			return 1;
	}
	return 0;
}

/**
 * find_source_device - search through device hierarchy for source device
 * @parent: pointer to Root Port pci_dev data structure
 * @e_info: including detailed error information such like id
 *
 * Return true if found.
 *
 * Invoked by DPC when error is detected at the Root Port.
 * Caller of this function must set id, severity, and multi_error_valid of
 * struct aer_err_info pointed by @e_info properly.  This function must fill
 * e_info->error_dev_num and e_info->dev[], based on the given information.
 */
static bool find_source_device(struct pci_dev *parent,
		struct aer_err_info *e_info)
{
	struct pci_dev *dev = parent;
	int result;

	/* Must reset in this function */
	e_info->error_dev_num = 0;

	/* Is Root Port an agent that sends error message? */
	result = find_device_iter(dev, e_info);
	if (result)
		return true;

	pci_walk_bus(parent->subordinate, find_device_iter, e_info);

	if (!e_info->error_dev_num) {
		pci_info(parent, "can't find device of ID%04x\n", e_info->id);
		return false;
	}
	return true;
}

/**
 * handle_error_source - handle logging error into an event log
 * @dev: pointer to pci_dev data structure of error source device
 * @info: comprehensive error information
 *
 * Invoked when an error being detected by Root Port.
 */
static void handle_error_source(struct pci_dev *dev, struct aer_err_info *info)
{
	int aer = dev->aer_cap;

	if (info->severity == AER_CORRECTABLE) {
		/*
		 * Correctable error does not need software intervention.
		 * No need to go through error recovery process.
		 */
		if (aer)
			pci_write_config_dword(dev, aer + PCI_ERR_COR_STATUS,
					info->status);
		if (pcie_aer_is_native(dev))
			pcie_clear_device_status(dev);
	} else if (info->severity == AER_NONFATAL)
		pcie_do_recovery(dev, pci_channel_io_normal, aer_root_reset);
	else if (info->severity == AER_FATAL)
		pcie_do_recovery(dev, pci_channel_io_frozen, aer_root_reset);
	pci_dev_put(dev);
}

#ifdef CONFIG_ACPI_APEI_PCIEAER

#define AER_RECOVER_RING_ORDER		4
#define AER_RECOVER_RING_SIZE		(1 << AER_RECOVER_RING_ORDER)

struct aer_recover_entry {
	u8	bus;
	u8	devfn;
	u16	domain;
	int	severity;
	struct aer_capability_regs *regs;
};

static DEFINE_KFIFO(aer_recover_ring, struct aer_recover_entry,
		    AER_RECOVER_RING_SIZE);

static void aer_recover_work_func(struct work_struct *work)
{
	struct aer_recover_entry entry;
	struct pci_dev *pdev;

	while (kfifo_get(&aer_recover_ring, &entry)) {
		pdev = pci_get_domain_bus_and_slot(entry.domain, entry.bus,
						   entry.devfn);
		if (!pdev) {
			pr_err("AER recover: Can not find pci_dev for %04x:%02x:%02x:%x\n",
			       entry.domain, entry.bus,
			       PCI_SLOT(entry.devfn), PCI_FUNC(entry.devfn));
			continue;
		}
		cper_print_aer(pdev, entry.severity, entry.regs);
		if (entry.severity == AER_NONFATAL)
			pcie_do_recovery(pdev, pci_channel_io_normal,
					 aer_root_reset);
		else if (entry.severity == AER_FATAL)
			pcie_do_recovery(pdev, pci_channel_io_frozen,
					 aer_root_reset);
		pci_dev_put(pdev);
	}
}

/*
 * Mutual exclusion for writers of aer_recover_ring, reader side don't
 * need lock, because there is only one reader and lock is not needed
 * between reader and writer.
 */
static DEFINE_SPINLOCK(aer_recover_ring_lock);
static DECLARE_WORK(aer_recover_work, aer_recover_work_func);

void aer_recover_queue(int domain, unsigned int bus, unsigned int devfn,
		       int severity, struct aer_capability_regs *aer_regs)
{
	struct aer_recover_entry entry = {
		.bus		= bus,
		.devfn		= devfn,
		.domain		= domain,
		.severity	= severity,
		.regs		= aer_regs,
	};

	if (kfifo_in_spinlocked(&aer_recover_ring, &entry, 1,
				 &aer_recover_ring_lock))
		schedule_work(&aer_recover_work);
	else
		pr_err("AER recover: Buffer overflow when recovering AER for %04x:%02x:%02x:%x\n",
		       domain, bus, PCI_SLOT(devfn), PCI_FUNC(devfn));
}
EXPORT_SYMBOL_GPL(aer_recover_queue);
#endif

/**
 * aer_get_device_error_info - read error status from dev and store it to info
 * @dev: pointer to the device expected to have a error record
 * @info: pointer to structure to store the error record
 *
 * Return 1 on success, 0 on error.
 *
 * Note that @info is reused among all error devices. Clear fields properly.
 */
int aer_get_device_error_info(struct pci_dev *dev, struct aer_err_info *info)
{
	int type = pci_pcie_type(dev);
	int aer = dev->aer_cap;
	int temp;

	/* Must reset in this function */
	info->status = 0;
	info->tlp_header_valid = 0;

	/* The device might not support AER */
	if (!aer)
		return 0;

	if (info->severity == AER_CORRECTABLE) {
		pci_read_config_dword(dev, aer + PCI_ERR_COR_STATUS,
			&info->status);
		pci_read_config_dword(dev, aer + PCI_ERR_COR_MASK,
			&info->mask);
		if (!(info->status & ~info->mask))
			return 0;
	} else if (type == PCI_EXP_TYPE_ROOT_PORT ||
		   type == PCI_EXP_TYPE_DOWNSTREAM ||
		   info->severity == AER_NONFATAL) {

		/* Link is still healthy for IO reads */
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS,
			&info->status);
		pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_MASK,
			&info->mask);
		if (!(info->status & ~info->mask))
			return 0;

		/* Get First Error Pointer */
		pci_read_config_dword(dev, aer + PCI_ERR_CAP, &temp);
		info->first_error = PCI_ERR_CAP_FEP(temp);

		if (info->status & AER_LOG_TLP_MASKS) {
			info->tlp_header_valid = 1;
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG, &info->tlp.dw0);
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG + 4, &info->tlp.dw1);
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG + 8, &info->tlp.dw2);
			pci_read_config_dword(dev,
				aer + PCI_ERR_HEADER_LOG + 12, &info->tlp.dw3);
		}
	}

	return 1;
}

static inline void aer_process_err_devices(struct aer_err_info *e_info)
{
	int i;

	/* Report all before handle them, not to lost records by reset etc. */
	for (i = 0; i < e_info->error_dev_num && e_info->dev[i]; i++) {
		if (aer_get_device_error_info(e_info->dev[i], e_info))
			aer_print_error(e_info->dev[i], e_info);
	}
	for (i = 0; i < e_info->error_dev_num && e_info->dev[i]; i++) {
		if (aer_get_device_error_info(e_info->dev[i], e_info))
			handle_error_source(e_info->dev[i], e_info);
	}
}

/**
 * aer_isr_one_error - consume an error detected by root port
 * @rpc: pointer to the root port which holds an error
 * @e_src: pointer to an error source
 */
static void aer_isr_one_error(struct aer_rpc *rpc,
		struct aer_err_source *e_src)
{
	struct pci_dev *pdev = rpc->rpd;
	struct aer_err_info e_info;

	pci_rootport_aer_stats_incr(pdev, e_src);

	/*
	 * There is a possibility that both correctable error and
	 * uncorrectable error being logged. Report correctable error first.
	 */
	if (e_src->status & PCI_ERR_ROOT_COR_RCV) {
		e_info.id = ERR_COR_ID(e_src->id);
		e_info.severity = AER_CORRECTABLE;

		if (e_src->status & PCI_ERR_ROOT_MULTI_COR_RCV)
			e_info.multi_error_valid = 1;
		else
			e_info.multi_error_valid = 0;
		aer_print_port_info(pdev, &e_info);

		if (find_source_device(pdev, &e_info))
			aer_process_err_devices(&e_info);
	}

	if (e_src->status & PCI_ERR_ROOT_UNCOR_RCV) {
		e_info.id = ERR_UNCOR_ID(e_src->id);

		if (e_src->status & PCI_ERR_ROOT_FATAL_RCV)
			e_info.severity = AER_FATAL;
		else
			e_info.severity = AER_NONFATAL;

		if (e_src->status & PCI_ERR_ROOT_MULTI_UNCOR_RCV)
			e_info.multi_error_valid = 1;
		else
			e_info.multi_error_valid = 0;

		aer_print_port_info(pdev, &e_info);

		if (find_source_device(pdev, &e_info))
			aer_process_err_devices(&e_info);
	}
}

/**
 * aer_isr - consume errors detected by root port
 * @irq: IRQ assigned to Root Port
 * @context: pointer to Root Port data structure
 *
 * Invoked, as DPC, when root port records new detected error
 */
static irqreturn_t aer_isr(int irq, void *context)
{
	struct pcie_device *dev = (struct pcie_device *)context;
	struct aer_rpc *rpc = get_service_data(dev);
	struct aer_err_source e_src;

	if (kfifo_is_empty(&rpc->aer_fifo))
		return IRQ_NONE;

	while (kfifo_get(&rpc->aer_fifo, &e_src))
		aer_isr_one_error(rpc, &e_src);
	return IRQ_HANDLED;
}

/**
 * aer_irq - Root Port's ISR
 * @irq: IRQ assigned to Root Port
 * @context: pointer to Root Port data structure
 *
 * Invoked when Root Port detects AER messages.
 */
static irqreturn_t aer_irq(int irq, void *context)
{
	struct pcie_device *pdev = (struct pcie_device *)context;
	struct aer_rpc *rpc = get_service_data(pdev);
	struct pci_dev *rp = rpc->rpd;
	int aer = rp->aer_cap;
	struct aer_err_source e_src = {};

	pci_read_config_dword(rp, aer + PCI_ERR_ROOT_STATUS, &e_src.status);
	if (!(e_src.status & AER_ERR_STATUS_MASK))
		return IRQ_NONE;

	pci_read_config_dword(rp, aer + PCI_ERR_ROOT_ERR_SRC, &e_src.id);
	pci_write_config_dword(rp, aer + PCI_ERR_ROOT_STATUS, e_src.status);

	if (!kfifo_put(&rpc->aer_fifo, e_src))
		return IRQ_HANDLED;

	return IRQ_WAKE_THREAD;
}

static int set_device_error_reporting(struct pci_dev *dev, void *data)
{
	bool enable = *((bool *)data);
	int type = pci_pcie_type(dev);

	if ((type == PCI_EXP_TYPE_ROOT_PORT) ||
	    (type == PCI_EXP_TYPE_RC_EC) ||
	    (type == PCI_EXP_TYPE_UPSTREAM) ||
	    (type == PCI_EXP_TYPE_DOWNSTREAM)) {
		if (enable)
			pci_enable_pcie_error_reporting(dev);
		else
			pci_disable_pcie_error_reporting(dev);
	}

	if (enable)
		pcie_set_ecrc_checking(dev);

	return 0;
}

/**
 * set_downstream_devices_error_reporting - enable/disable the error reporting  bits on the root port and its downstream ports.
 * @dev: pointer to root port's pci_dev data structure
 * @enable: true = enable error reporting, false = disable error reporting.
 */
static void set_downstream_devices_error_reporting(struct pci_dev *dev,
						   bool enable)
{
	set_device_error_reporting(dev, &enable);

	if (!dev->subordinate)
		return;
	pci_walk_bus(dev->subordinate, set_device_error_reporting, &enable);
}

/**
 * aer_enable_rootport - enable Root Port's interrupts when receiving messages
 * @rpc: pointer to a Root Port data structure
 *
 * Invoked when PCIe bus loads AER service driver.
 */
static void aer_enable_rootport(struct aer_rpc *rpc)
{
	struct pci_dev *pdev = rpc->rpd;
	int aer = pdev->aer_cap;
	u16 reg16;
	u32 reg32;

	/* Clear PCIe Capability's Device Status */
	pcie_capability_read_word(pdev, PCI_EXP_DEVSTA, &reg16);
	pcie_capability_write_word(pdev, PCI_EXP_DEVSTA, reg16);

	/* Disable system error generation in response to error messages */
	pcie_capability_clear_word(pdev, PCI_EXP_RTCTL,
				   SYSTEM_ERROR_INTR_ON_MESG_MASK);

	/* Clear error status */
	pci_read_config_dword(pdev, aer + PCI_ERR_ROOT_STATUS, &reg32);
	pci_write_config_dword(pdev, aer + PCI_ERR_ROOT_STATUS, reg32);
	pci_read_config_dword(pdev, aer + PCI_ERR_COR_STATUS, &reg32);
	pci_write_config_dword(pdev, aer + PCI_ERR_COR_STATUS, reg32);
	pci_read_config_dword(pdev, aer + PCI_ERR_UNCOR_STATUS, &reg32);
	pci_write_config_dword(pdev, aer + PCI_ERR_UNCOR_STATUS, reg32);

	/*
	 * Enable error reporting for the root port device and downstream port
	 * devices.
	 */
	set_downstream_devices_error_reporting(pdev, true);

	/* Enable Root Port's interrupt in response to error messages */
	pci_read_config_dword(pdev, aer + PCI_ERR_ROOT_COMMAND, &reg32);
	reg32 |= ROOT_PORT_INTR_ON_MESG_MASK;
	pci_write_config_dword(pdev, aer + PCI_ERR_ROOT_COMMAND, reg32);
}

/**
 * aer_disable_rootport - disable Root Port's interrupts when receiving messages
 * @rpc: pointer to a Root Port data structure
 *
 * Invoked when PCIe bus unloads AER service driver.
 */
static void aer_disable_rootport(struct aer_rpc *rpc)
{
	struct pci_dev *pdev = rpc->rpd;
	int aer = pdev->aer_cap;
	u32 reg32;

	/*
	 * Disable error reporting for the root port device and downstream port
	 * devices.
	 */
	set_downstream_devices_error_reporting(pdev, false);

	/* Disable Root's interrupt in response to error messages */
	pci_read_config_dword(pdev, aer + PCI_ERR_ROOT_COMMAND, &reg32);
	reg32 &= ~ROOT_PORT_INTR_ON_MESG_MASK;
	pci_write_config_dword(pdev, aer + PCI_ERR_ROOT_COMMAND, reg32);

	/* Clear Root's error status reg */
	pci_read_config_dword(pdev, aer + PCI_ERR_ROOT_STATUS, &reg32);
	pci_write_config_dword(pdev, aer + PCI_ERR_ROOT_STATUS, reg32);
}

/**
 * aer_remove - clean up resources
 * @dev: pointer to the pcie_dev data structure
 *
 * Invoked when PCI Express bus unloads or AER probe fails.
 */
static void aer_remove(struct pcie_device *dev)
{
	struct aer_rpc *rpc = get_service_data(dev);

	aer_disable_rootport(rpc);
}

/**
 * aer_probe - initialize resources
 * @dev: pointer to the pcie_dev data structure
 *
 * Invoked when PCI Express bus loads AER service driver.
 */
static int aer_probe(struct pcie_device *dev)
{
	int status;
	struct aer_rpc *rpc;
	struct device *device = &dev->device;
	struct pci_dev *port = dev->port;

	/* Limit to Root Ports or Root Complex Event Collectors */
	if ((pci_pcie_type(port) != PCI_EXP_TYPE_RC_EC) &&
	    (pci_pcie_type(port) != PCI_EXP_TYPE_ROOT_PORT))
		return -ENODEV;

	rpc = devm_kzalloc(device, sizeof(struct aer_rpc), GFP_KERNEL);
	if (!rpc)
		return -ENOMEM;

	rpc->rpd = port;
	INIT_KFIFO(rpc->aer_fifo);
	set_service_data(dev, rpc);

	status = devm_request_threaded_irq(device, dev->irq, aer_irq, aer_isr,
					   IRQF_SHARED, "aerdrv", dev);
	if (status) {
		pci_err(port, "request AER IRQ %d failed\n", dev->irq);
		return status;
	}

	aer_enable_rootport(rpc);
	pci_info(port, "enabled with IRQ %d\n", dev->irq);
	return 0;
}

/**
 * aer_root_reset - reset Root Port hierarchy or RCEC
 * @dev: pointer to Root Port or RCEC
 *
 * Invoked by Port Bus driver when performing reset.
 */
static pci_ers_result_t aer_root_reset(struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);
	struct pci_dev *root;
	int aer;
	struct pci_host_bridge *host = pci_find_host_bridge(dev->bus);
	u32 reg32;
	int rc;

	root = dev;	/* device with Root Error registers */
	aer = root->aer_cap;

	if ((host->native_aer || pcie_ports_native) && aer) {
		/* Disable Root's interrupt in response to error messages */
		pci_read_config_dword(root, aer + PCI_ERR_ROOT_COMMAND, &reg32);
		reg32 &= ~ROOT_PORT_INTR_ON_MESG_MASK;
		pci_write_config_dword(root, aer + PCI_ERR_ROOT_COMMAND, reg32);
	}

	if (type == PCI_EXP_TYPE_RC_EC) {
		if (pcie_has_flr(dev)) {
			rc = pcie_flr(dev);
			pci_info(dev, "has been reset (%d)\n", rc);
		} else {
			pci_info(dev, "not reset (no FLR support)\n");
			rc = -ENOTTY;
		}
	} else {
		rc = pci_bus_error_reset(dev);
		pci_info(dev, "Root Port link has been reset (%d)\n", rc);
	}

	if ((host->native_aer || pcie_ports_native) && aer) {
		/* Clear Root Error Status */
		pci_read_config_dword(root, aer + PCI_ERR_ROOT_STATUS, &reg32);
		pci_write_config_dword(root, aer + PCI_ERR_ROOT_STATUS, reg32);

		/* Enable Root Port's interrupt in response to error messages */
		pci_read_config_dword(root, aer + PCI_ERR_ROOT_COMMAND, &reg32);
		reg32 |= ROOT_PORT_INTR_ON_MESG_MASK;
		pci_write_config_dword(root, aer + PCI_ERR_ROOT_COMMAND, reg32);
	}

	return rc ? PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_RECOVERED;
}

static struct pcie_port_service_driver aerdriver = {
	.name		= "aer",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_AER,

	.probe		= aer_probe,
	.remove		= aer_remove,
};

/**
 * aer_service_init - register AER root service driver
 *
 * Invoked when AER root service driver is loaded.
 */
int __init pcie_aer_init(void)
{
	if (!pci_aer_available())
		return -ENXIO;
	return pcie_port_service_register(&aerdriver);
}

/*
 * drivers/pci/pcie/aer/aerdrv_errprint.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Format error messages and print them to console.
 *
 * Copyright (C) 2006 Intel Corp.
 *	Tom Long Nguyen (tom.l.nguyen@intel.com)
 *	Zhang Yanmin (yanmin.zhang@intel.com)
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/cper.h>

#include "aerdrv.h"

#define CREATE_TRACE_POINTS
#include <trace/events/ras.h>

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
	"Receiver Error",		/* Bit Position 0	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Bad TLP",			/* Bit Position 6	*/
	"Bad DLLP",			/* Bit Position 7	*/
	"RELAY_NUM Rollover",		/* Bit Position 8	*/
	NULL,
	NULL,
	NULL,
	"Replay Timer Timeout",		/* Bit Position 12	*/
	"Advisory Non-Fatal",		/* Bit Position 13	*/
};

static const char *aer_uncorrectable_error_string[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	"Data Link Protocol",		/* Bit Position 4	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Poisoned TLP",			/* Bit Position 12	*/
	"Flow Control Protocol",	/* Bit Position 13	*/
	"Completion Timeout",		/* Bit Position 14	*/
	"Completer Abort",		/* Bit Position 15	*/
	"Unexpected Completion",	/* Bit Position 16	*/
	"Receiver Overflow",		/* Bit Position 17	*/
	"Malformed TLP",		/* Bit Position 18	*/
	"ECRC",				/* Bit Position 19	*/
	"Unsupported Request",		/* Bit Position 20	*/
};

static const char *aer_agent_string[] = {
	"Receiver ID",
	"Requester ID",
	"Completer ID",
	"Transmitter ID"
};

static void __aer_print_error(struct pci_dev *dev,
			      struct aer_err_info *info)
{
	int i, status;
	const char *errmsg = NULL;
	status = (info->status & ~info->mask);

	for (i = 0; i < 32; i++) {
		if (!(status & (1 << i)))
			continue;

		if (info->severity == AER_CORRECTABLE)
			errmsg = i < ARRAY_SIZE(aer_correctable_error_string) ?
				aer_correctable_error_string[i] : NULL;
		else
			errmsg = i < ARRAY_SIZE(aer_uncorrectable_error_string) ?
				aer_uncorrectable_error_string[i] : NULL;

		if (errmsg)
			dev_err(&dev->dev, "   [%2d] %-22s%s\n", i, errmsg,
				info->first_error == i ? " (First)" : "");
		else
			dev_err(&dev->dev, "   [%2d] Unknown Error Bit%s\n",
				i, info->first_error == i ? " (First)" : "");
	}
}

void aer_print_error(struct pci_dev *dev, struct aer_err_info *info)
{
	int id = ((dev->bus->number << 8) | dev->devfn);

	if (info->status == 0) {
		dev_err(&dev->dev,
			"PCIe Bus Error: severity=%s, type=Unaccessible, "
			"id=%04x(Unregistered Agent ID)\n",
			aer_error_severity_string[info->severity], id);
	} else {
		int layer, agent;

		layer = AER_GET_LAYER_ERROR(info->severity, info->status);
		agent = AER_GET_AGENT(info->severity, info->status);

		dev_err(&dev->dev,
			"PCIe Bus Error: severity=%s, type=%s, id=%04x(%s)\n",
			aer_error_severity_string[info->severity],
			aer_error_layer[layer], id, aer_agent_string[agent]);

		dev_err(&dev->dev,
			"  device [%04x:%04x] error status/mask=%08x/%08x\n",
			dev->vendor, dev->device,
			info->status, info->mask);

		__aer_print_error(dev, info);

		if (info->tlp_header_valid) {
			unsigned char *tlp = (unsigned char *) &info->tlp;
			dev_err(&dev->dev, "  TLP Header:"
				" %02x%02x%02x%02x %02x%02x%02x%02x"
				" %02x%02x%02x%02x %02x%02x%02x%02x\n",
				*(tlp + 3), *(tlp + 2), *(tlp + 1), *tlp,
				*(tlp + 7), *(tlp + 6), *(tlp + 5), *(tlp + 4),
				*(tlp + 11), *(tlp + 10), *(tlp + 9),
				*(tlp + 8), *(tlp + 15), *(tlp + 14),
				*(tlp + 13), *(tlp + 12));
		}
	}

	if (info->id && info->error_dev_num > 1 && info->id == id)
		dev_err(&dev->dev,
			   "  Error of this Agent(%04x) is reported first\n",
			id);
	trace_aer_event(dev_name(&dev->dev), (info->status & ~info->mask),
			info->severity);
}

void aer_print_port_info(struct pci_dev *dev, struct aer_err_info *info)
{
	dev_info(&dev->dev, "AER: %s%s error received: id=%04x\n",
		info->multi_error_valid ? "Multiple " : "",
		aer_error_severity_string[info->severity], info->id);
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

void cper_print_aer(const char *prefix, struct pci_dev *dev, int cper_severity,
		    struct aer_capability_regs *aer)
{
	int aer_severity, layer, agent, status_strs_size, tlp_header_valid = 0;
	u32 status, mask;
	const char **status_strs;

	aer_severity = cper_severity_to_aer(cper_severity);
	if (aer_severity == AER_CORRECTABLE) {
		status = aer->cor_status;
		mask = aer->cor_mask;
		status_strs = aer_correctable_error_string;
		status_strs_size = ARRAY_SIZE(aer_correctable_error_string);
	} else {
		status = aer->uncor_status;
		mask = aer->uncor_mask;
		status_strs = aer_uncorrectable_error_string;
		status_strs_size = ARRAY_SIZE(aer_uncorrectable_error_string);
		tlp_header_valid = status & AER_LOG_TLP_MASKS;
	}
	layer = AER_GET_LAYER_ERROR(aer_severity, status);
	agent = AER_GET_AGENT(aer_severity, status);
	dev_err(&dev->dev, "aer_status: 0x%08x, aer_mask: 0x%08x\n",
	       status, mask);
	cper_print_bits(prefix, status, status_strs, status_strs_size);
	dev_err(&dev->dev, "aer_layer=%s, aer_agent=%s\n",
	       aer_error_layer[layer], aer_agent_string[agent]);
	if (aer_severity != AER_CORRECTABLE)
		dev_err(&dev->dev, "aer_uncor_severity: 0x%08x\n",
		       aer->uncor_severity);
	if (tlp_header_valid) {
		const unsigned char *tlp;
		tlp = (const unsigned char *)&aer->header_log;
		dev_err(&dev->dev, "aer_tlp_header:"
			" %02x%02x%02x%02x %02x%02x%02x%02x"
			" %02x%02x%02x%02x %02x%02x%02x%02x\n",
			*(tlp + 3), *(tlp + 2), *(tlp + 1), *tlp,
			*(tlp + 7), *(tlp + 6), *(tlp + 5), *(tlp + 4),
			*(tlp + 11), *(tlp + 10), *(tlp + 9),
			*(tlp + 8), *(tlp + 15), *(tlp + 14),
			*(tlp + 13), *(tlp + 12));
	}
	trace_aer_event(dev_name(&dev->dev), (status & ~mask),
			aer_severity);
}
#endif

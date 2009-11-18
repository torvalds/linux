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

#include "aerdrv.h"

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

#define AER_PR(info, pdev, fmt, args...)				\
	printk("%s%s %s: " fmt, (info->severity == AER_CORRECTABLE) ?	\
		KERN_WARNING : KERN_ERR, dev_driver_string(&pdev->dev),	\
		dev_name(&pdev->dev), ## args)

/*
 * AER error strings
 */
static char *aer_error_severity_string[] = {
	"Uncorrected (Non-Fatal)",
	"Uncorrected (Fatal)",
	"Corrected"
};

static char *aer_error_layer[] = {
	"Physical Layer",
	"Data Link Layer",
	"Transaction Layer"
};
static char *aer_correctable_error_string[] = {
	"Receiver Error        ",	/* Bit Position 0	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Bad TLP               ",	/* Bit Position 6	*/
	"Bad DLLP              ",	/* Bit Position 7	*/
	"RELAY_NUM Rollover    ",	/* Bit Position 8	*/
	NULL,
	NULL,
	NULL,
	"Replay Timer Timeout  ",	/* Bit Position 12	*/
	"Advisory Non-Fatal    ",	/* Bit Position 13	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

static char *aer_uncorrectable_error_string[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	"Data Link Protocol    ",	/* Bit Position 4	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Poisoned TLP          ",	/* Bit Position 12	*/
	"Flow Control Protocol ",	/* Bit Position 13	*/
	"Completion Timeout    ",	/* Bit Position 14	*/
	"Completer Abort       ",	/* Bit Position 15	*/
	"Unexpected Completion ",	/* Bit Position 16	*/
	"Receiver Overflow     ",	/* Bit Position 17	*/
	"Malformed TLP         ",	/* Bit Position 18	*/
	"ECRC                  ",	/* Bit Position 19	*/
	"Unsupported Request   ",	/* Bit Position 20	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

static char *aer_agent_string[] = {
	"Receiver ID",
	"Requester ID",
	"Completer ID",
	"Transmitter ID"
};

static void __aer_print_error(struct aer_err_info *info, struct pci_dev *dev)
{
	int i, status;
	char *errmsg = NULL;

	status = (info->status & ~info->mask);

	for (i = 0; i < 32; i++) {
		if (!(status & (1 << i)))
			continue;

		if (info->severity == AER_CORRECTABLE)
			errmsg = aer_correctable_error_string[i];
		else
			errmsg = aer_uncorrectable_error_string[i];

		if (errmsg)
			AER_PR(info, dev, "   [%2d] %s%s\n", i, errmsg,
				info->first_error == i ? " (First)" : "");
		else
			AER_PR(info, dev, "   [%2d] Unknown Error Bit%s\n", i,
				info->first_error == i ? " (First)" : "");
	}
}

void aer_print_error(struct pci_dev *dev, struct aer_err_info *info)
{
	int id = ((dev->bus->number << 8) | dev->devfn);

	if (info->status == 0) {
		AER_PR(info, dev,
			"PCIE Bus Error: severity=%s, type=Unaccessible, "
			"id=%04x(Unregistered Agent ID)\n",
			aer_error_severity_string[info->severity], id);
	} else {
		int layer, agent;

		layer = AER_GET_LAYER_ERROR(info->severity, info->status);
		agent = AER_GET_AGENT(info->severity, info->status);

		AER_PR(info, dev,
			"PCIE Bus Error: severity=%s, type=%s, id=%04x(%s)\n",
			aer_error_severity_string[info->severity],
			aer_error_layer[layer], id, aer_agent_string[agent]);

		AER_PR(info, dev,
			"  device [%04x:%04x] error status/mask=%08x/%08x\n",
			dev->vendor, dev->device, info->status, info->mask);

		__aer_print_error(info, dev);

		if (info->tlp_header_valid) {
			unsigned char *tlp = (unsigned char *) &info->tlp;
			AER_PR(info, dev, "  TLP Header:"
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
		AER_PR(info, dev,
			"  Error of this Agent(%04x) is reported first\n", id);
}

void aer_print_port_info(struct pci_dev *dev, struct aer_err_info *info)
{
	dev_info(&dev->dev, "AER: %s%s error received: id=%04x\n",
		info->multi_error_valid ? "Multiple " : "",
		aer_error_severity_string[info->severity], info->id);
}

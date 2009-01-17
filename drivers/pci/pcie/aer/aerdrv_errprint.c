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

#define AER_AGENT_REQUESTER_MASK	(PCI_ERR_UNC_COMP_TIME|	\
					PCI_ERR_UNC_UNSUP)

#define AER_AGENT_COMPLETER_MASK	PCI_ERR_UNC_COMP_ABORT

#define AER_AGENT_TRANSMITTER_MASK(t, e) (e & (PCI_ERR_COR_REP_ROLL| \
	((t == AER_CORRECTABLE) ? PCI_ERR_COR_REP_TIMER: 0)))

#define AER_GET_AGENT(t, e)						\
	((e & AER_AGENT_COMPLETER_MASK) ? AER_AGENT_COMPLETER :		\
	(e & AER_AGENT_REQUESTER_MASK) ? AER_AGENT_REQUESTER :		\
	(AER_AGENT_TRANSMITTER_MASK(t, e)) ? AER_AGENT_TRANSMITTER :	\
	AER_AGENT_RECEIVER)

#define AER_PHYSICAL_LAYER_ERROR_MASK	PCI_ERR_COR_RCVR
#define AER_DATA_LINK_LAYER_ERROR_MASK(t, e)	\
		(PCI_ERR_UNC_DLP|		\
		PCI_ERR_COR_BAD_TLP| 		\
		PCI_ERR_COR_BAD_DLLP|		\
		PCI_ERR_COR_REP_ROLL| 		\
		((t == AER_CORRECTABLE) ?	\
		PCI_ERR_COR_REP_TIMER: 0))

#define AER_PHYSICAL_LAYER_ERROR	0
#define AER_DATA_LINK_LAYER_ERROR	1
#define AER_TRANSACTION_LAYER_ERROR	2

#define AER_GET_LAYER_ERROR(t, e)				\
	((e & AER_PHYSICAL_LAYER_ERROR_MASK) ?			\
	AER_PHYSICAL_LAYER_ERROR :				\
	(e & AER_DATA_LINK_LAYER_ERROR_MASK(t, e)) ?		\
		AER_DATA_LINK_LAYER_ERROR : 			\
		AER_TRANSACTION_LAYER_ERROR)

/*
 * AER error strings
 */
static char* aer_error_severity_string[] = {
	"Uncorrected (Non-Fatal)",
	"Uncorrected (Fatal)",
	"Corrected"
};

static char* aer_error_layer[] = {
	"Physical Layer",
	"Data Link Layer",
	"Transaction Layer"
};
static char* aer_correctable_error_string[] = {
	"Receiver Error        ",	/* Bit Position 0 	*/
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Bad TLP               ",	/* Bit Position 6 	*/
	"Bad DLLP              ",	/* Bit Position 7 	*/
	"RELAY_NUM Rollover    ",	/* Bit Position 8 	*/
	NULL,
	NULL,
	NULL,
	"Replay Timer Timeout  ",	/* Bit Position 12 	*/
	"Advisory Non-Fatal    ", 	/* Bit Position 13	*/
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

static char* aer_uncorrectable_error_string[] = {
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
	"Poisoned TLP          ",	/* Bit Position 12 	*/
	"Flow Control Protocol ",	/* Bit Position 13	*/
	"Completion Timeout    ",	/* Bit Position 14 	*/
	"Completer Abort       ",	/* Bit Position 15 	*/
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

static char* aer_agent_string[] = {
	"Receiver ID",
	"Requester ID",
	"Completer ID",
	"Transmitter ID"
};

static char * aer_get_error_source_name(int severity,
			unsigned int status,
			char errmsg_buff[])
{
	int i;
	char * errmsg = NULL;

	for (i = 0; i < 32; i++) {
		if (!(status & (1 << i)))
			continue;

		if (severity == AER_CORRECTABLE)
			errmsg = aer_correctable_error_string[i];
		else
			errmsg = aer_uncorrectable_error_string[i];

		if (!errmsg) {
			sprintf(errmsg_buff, "Unknown Error Bit %2d  ", i);
			errmsg = errmsg_buff;
		}

		break;
	}

	return errmsg;
}

static DEFINE_SPINLOCK(logbuf_lock);
static char errmsg_buff[100];
void aer_print_error(struct pci_dev *dev, struct aer_err_info *info)
{
	char * errmsg;
	int err_layer, agent;
	char * loglevel;

	if (info->severity == AER_CORRECTABLE)
		loglevel = KERN_WARNING;
	else
		loglevel = KERN_ERR;

	printk("%s+------ PCI-Express Device Error ------+\n", loglevel);
	printk("%sError Severity\t\t: %s\n", loglevel,
		aer_error_severity_string[info->severity]);

	if ( info->status == 0) {
		printk("%sPCIE Bus Error type\t: (Unaccessible)\n", loglevel);
		printk("%sUnaccessible Received\t: %s\n", loglevel,
			info->flags & AER_MULTI_ERROR_VALID_FLAG ?
				"Multiple" : "First");
		printk("%sUnregistered Agent ID\t: %04x\n", loglevel,
			(dev->bus->number << 8) | dev->devfn);
	} else {
		err_layer = AER_GET_LAYER_ERROR(info->severity, info->status);
		printk("%sPCIE Bus Error type\t: %s\n", loglevel,
			aer_error_layer[err_layer]);

		spin_lock(&logbuf_lock);
		errmsg = aer_get_error_source_name(info->severity,
				info->status,
				errmsg_buff);
		printk("%s%s\t: %s\n", loglevel, errmsg,
			info->flags & AER_MULTI_ERROR_VALID_FLAG ?
				"Multiple" : "First");
		spin_unlock(&logbuf_lock);

		agent = AER_GET_AGENT(info->severity, info->status);
		printk("%s%s\t\t: %04x\n", loglevel,
			aer_agent_string[agent],
			(dev->bus->number << 8) | dev->devfn);

		printk("%sVendorID=%04xh, DeviceID=%04xh,"
			" Bus=%02xh, Device=%02xh, Function=%02xh\n",
			loglevel,
			dev->vendor,
			dev->device,
			dev->bus->number,
			PCI_SLOT(dev->devfn),
			PCI_FUNC(dev->devfn));

		if (info->flags & AER_TLP_HEADER_VALID_FLAG) {
			unsigned char *tlp = (unsigned char *) &info->tlp;
			printk("%sTLP Header:\n", loglevel);
			printk("%s%02x%02x%02x%02x %02x%02x%02x%02x"
				" %02x%02x%02x%02x %02x%02x%02x%02x\n",
				loglevel,
				*(tlp + 3), *(tlp + 2), *(tlp + 1), *tlp,
				*(tlp + 7), *(tlp + 6), *(tlp + 5), *(tlp + 4),
				*(tlp + 11), *(tlp + 10), *(tlp + 9),
				*(tlp + 8), *(tlp + 15), *(tlp + 14),
				*(tlp + 13), *(tlp + 12));
		}
	}
}


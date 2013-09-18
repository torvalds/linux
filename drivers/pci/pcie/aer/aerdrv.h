/*
 * Copyright (C) 2006 Intel Corp.
 *	Tom Long Nguyen (tom.l.nguyen@intel.com)
 *	Zhang Yanmin (yanmin.zhang@intel.com)
 *
 */

#ifndef _AERDRV_H_
#define _AERDRV_H_

#include <linux/workqueue.h>
#include <linux/pcieport_if.h>
#include <linux/aer.h>
#include <linux/interrupt.h>

#define SYSTEM_ERROR_INTR_ON_MESG_MASK	(PCI_EXP_RTCTL_SECEE|	\
					PCI_EXP_RTCTL_SENFEE|	\
					PCI_EXP_RTCTL_SEFEE)
#define ROOT_PORT_INTR_ON_MESG_MASK	(PCI_ERR_ROOT_CMD_COR_EN|	\
					PCI_ERR_ROOT_CMD_NONFATAL_EN|	\
					PCI_ERR_ROOT_CMD_FATAL_EN)
#define ERR_COR_ID(d)			(d & 0xffff)
#define ERR_UNCOR_ID(d)			(d >> 16)

#define AER_ERROR_SOURCES_MAX		100

#define AER_LOG_TLP_MASKS		(PCI_ERR_UNC_POISON_TLP|	\
					PCI_ERR_UNC_ECRC|		\
					PCI_ERR_UNC_UNSUP|		\
					PCI_ERR_UNC_COMP_ABORT|		\
					PCI_ERR_UNC_UNX_COMP|		\
					PCI_ERR_UNC_MALF_TLP)

#define AER_MAX_MULTI_ERR_DEVICES	5	/* Not likely to have more */
struct aer_err_info {
	struct pci_dev *dev[AER_MAX_MULTI_ERR_DEVICES];
	int error_dev_num;

	unsigned int id:16;

	unsigned int severity:2;	/* 0:NONFATAL | 1:FATAL | 2:COR */
	unsigned int __pad1:5;
	unsigned int multi_error_valid:1;

	unsigned int first_error:5;
	unsigned int __pad2:2;
	unsigned int tlp_header_valid:1;

	unsigned int status;		/* COR/UNCOR Error Status */
	unsigned int mask;		/* COR/UNCOR Error Mask */
	struct aer_header_log_regs tlp;	/* TLP Header */
};

struct aer_err_source {
	unsigned int status;
	unsigned int id;
};

struct aer_rpc {
	struct pcie_device *rpd;	/* Root Port device */
	struct work_struct dpc_handler;
	struct aer_err_source e_sources[AER_ERROR_SOURCES_MAX];
	unsigned short prod_idx;	/* Error Producer Index */
	unsigned short cons_idx;	/* Error Consumer Index */
	int isr;
	spinlock_t e_lock;		/*
					 * Lock access to Error Status/ID Regs
					 * and error producer/consumer index
					 */
	struct mutex rpc_mutex;		/*
					 * only one thread could do
					 * recovery on the same
					 * root port hierarchy
					 */
	wait_queue_head_t wait_release;
};

struct aer_broadcast_data {
	enum pci_channel_state state;
	enum pci_ers_result result;
};

static inline pci_ers_result_t merge_result(enum pci_ers_result orig,
		enum pci_ers_result new)
{
	if (new == PCI_ERS_RESULT_NO_AER_DRIVER)
		return PCI_ERS_RESULT_NO_AER_DRIVER;

	if (new == PCI_ERS_RESULT_NONE)
		return orig;

	switch (orig) {
	case PCI_ERS_RESULT_CAN_RECOVER:
	case PCI_ERS_RESULT_RECOVERED:
		orig = new;
		break;
	case PCI_ERS_RESULT_DISCONNECT:
		if (new == PCI_ERS_RESULT_NEED_RESET)
			orig = PCI_ERS_RESULT_NEED_RESET;
		break;
	default:
		break;
	}

	return orig;
}

extern struct bus_type pcie_port_bus_type;
void aer_do_secondary_bus_reset(struct pci_dev *dev);
int aer_init(struct pcie_device *dev);
void aer_isr(struct work_struct *work);
void aer_print_error(struct pci_dev *dev, struct aer_err_info *info);
void aer_print_port_info(struct pci_dev *dev, struct aer_err_info *info);
irqreturn_t aer_irq(int irq, void *context);

#ifdef CONFIG_ACPI_APEI
int pcie_aer_get_firmware_first(struct pci_dev *pci_dev);
#else
static inline int pcie_aer_get_firmware_first(struct pci_dev *pci_dev)
{
	if (pci_dev->__aer_firmware_first_valid)
		return pci_dev->__aer_firmware_first;
	return 0;
}
#endif

static inline void pcie_aer_force_firmware_first(struct pci_dev *pci_dev,
						 int enable)
{
	pci_dev->__aer_firmware_first = !!enable;
	pci_dev->__aer_firmware_first_valid = 1;
}
#endif /* _AERDRV_H_ */

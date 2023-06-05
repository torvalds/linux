// SPDX-License-Identifier: GPL-2.0
/*
 * Endpoint Function Driver to implement Non-Transparent Bridge functionality
 * Between PCI RC and EP
 *
 * Copyright (C) 2020 Texas Instruments
 * Copyright (C) 2022 NXP
 *
 * Based on pci-epf-ntb.c
 * Author: Frank Li <Frank.Li@nxp.com>
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

/*
 * +------------+         +---------------------------------------+
 * |            |         |                                       |
 * +------------+         |                        +--------------+
 * | NTB        |         |                        | NTB          |
 * | NetDev     |         |                        | NetDev       |
 * +------------+         |                        +--------------+
 * | NTB        |         |                        | NTB          |
 * | Transfer   |         |                        | Transfer     |
 * +------------+         |                        +--------------+
 * |            |         |                        |              |
 * |  PCI NTB   |         |                        |              |
 * |    EPF     |         |                        |              |
 * |   Driver   |         |                        | PCI Virtual  |
 * |            |         +---------------+        | NTB Driver   |
 * |            |         | PCI EP NTB    |<------>|              |
 * |            |         |  FN Driver    |        |              |
 * +------------+         +---------------+        +--------------+
 * |            |         |               |        |              |
 * |  PCI Bus   | <-----> |  PCI EP Bus   |        |  Virtual PCI |
 * |            |  PCI    |               |        |     Bus      |
 * +------------+         +---------------+--------+--------------+
 * PCIe Root Port                        PCI EP
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/ntb.h>

static struct workqueue_struct *kpcintb_workqueue;

#define COMMAND_CONFIGURE_DOORBELL	1
#define COMMAND_TEARDOWN_DOORBELL	2
#define COMMAND_CONFIGURE_MW		3
#define COMMAND_TEARDOWN_MW		4
#define COMMAND_LINK_UP			5
#define COMMAND_LINK_DOWN		6

#define COMMAND_STATUS_OK		1
#define COMMAND_STATUS_ERROR		2

#define LINK_STATUS_UP			BIT(0)

#define SPAD_COUNT			64
#define DB_COUNT			4
#define NTB_MW_OFFSET			2
#define DB_COUNT_MASK			GENMASK(15, 0)
#define MSIX_ENABLE			BIT(16)
#define MAX_DB_COUNT			32
#define MAX_MW				4

enum epf_ntb_bar {
	BAR_CONFIG,
	BAR_DB,
	BAR_MW0,
	BAR_MW1,
	BAR_MW2,
};

/*
 * +--------------------------------------------------+ Base
 * |                                                  |
 * |                                                  |
 * |                                                  |
 * |          Common Control Register                 |
 * |                                                  |
 * |                                                  |
 * |                                                  |
 * +-----------------------+--------------------------+ Base+span_offset
 * |                       |                          |
 * |    Peer Span Space    |    Span Space            |
 * |                       |                          |
 * |                       |                          |
 * +-----------------------+--------------------------+ Base+span_offset
 * |                       |                          |     +span_count * 4
 * |                       |                          |
 * |     Span Space        |   Peer Span Space        |
 * |                       |                          |
 * +-----------------------+--------------------------+
 *       Virtual PCI             PCIe Endpoint
 *       NTB Driver               NTB Driver
 */
struct epf_ntb_ctrl {
	u32 command;
	u32 argument;
	u16 command_status;
	u16 link_status;
	u32 topology;
	u64 addr;
	u64 size;
	u32 num_mws;
	u32 reserved;
	u32 spad_offset;
	u32 spad_count;
	u32 db_entry_size;
	u32 db_data[MAX_DB_COUNT];
	u32 db_offset[MAX_DB_COUNT];
} __packed;

struct epf_ntb {
	struct ntb_dev ntb;
	struct pci_epf *epf;
	struct config_group group;

	u32 num_mws;
	u32 db_count;
	u32 spad_count;
	u64 mws_size[MAX_MW];
	u64 db;
	u32 vbus_number;
	u16 vntb_pid;
	u16 vntb_vid;

	bool linkup;
	u32 spad_size;

	enum pci_barno epf_ntb_bar[6];

	struct epf_ntb_ctrl *reg;

	u32 *epf_db;

	phys_addr_t vpci_mw_phy[MAX_MW];
	void __iomem *vpci_mw_addr[MAX_MW];

	struct delayed_work cmd_handler;
};

#define to_epf_ntb(epf_group) container_of((epf_group), struct epf_ntb, group)
#define ntb_ndev(__ntb) container_of(__ntb, struct epf_ntb, ntb)

static struct pci_epf_header epf_ntb_header = {
	.vendorid	= PCI_ANY_ID,
	.deviceid	= PCI_ANY_ID,
	.baseclass_code	= PCI_BASE_CLASS_MEMORY,
	.interrupt_pin	= PCI_INTERRUPT_INTA,
};

/**
 * epf_ntb_link_up() - Raise link_up interrupt to Virtual Host (VHOST)
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 * @link_up: true or false indicating Link is UP or Down
 *
 * Once NTB function in HOST invoke ntb_link_enable(),
 * this NTB function driver will trigger a link event to VHOST.
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_link_up(struct epf_ntb *ntb, bool link_up)
{
	if (link_up)
		ntb->reg->link_status |= LINK_STATUS_UP;
	else
		ntb->reg->link_status &= ~LINK_STATUS_UP;

	ntb_link_event(&ntb->ntb);
	return 0;
}

/**
 * epf_ntb_configure_mw() - Configure the Outbound Address Space for VHOST
 *   to access the memory window of HOST
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 * @mw: Index of the memory window (either 0, 1, 2 or 3)
 *
 *                          EP Outbound Window
 * +--------+              +-----------+
 * |        |              |           |
 * |        |              |           |
 * |        |              |           |
 * |        |              |           |
 * |        |              +-----------+
 * | Virtual|              | Memory Win|
 * | NTB    | -----------> |           |
 * | Driver |              |           |
 * |        |              +-----------+
 * |        |              |           |
 * |        |              |           |
 * +--------+              +-----------+
 *  VHOST                   PCI EP
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_configure_mw(struct epf_ntb *ntb, u32 mw)
{
	phys_addr_t phys_addr;
	u8 func_no, vfunc_no;
	u64 addr, size;
	int ret = 0;

	phys_addr = ntb->vpci_mw_phy[mw];
	addr = ntb->reg->addr;
	size = ntb->reg->size;

	func_no = ntb->epf->func_no;
	vfunc_no = ntb->epf->vfunc_no;

	ret = pci_epc_map_addr(ntb->epf->epc, func_no, vfunc_no, phys_addr, addr, size);
	if (ret)
		dev_err(&ntb->epf->epc->dev,
			"Failed to map memory window %d address\n", mw);
	return ret;
}

/**
 * epf_ntb_teardown_mw() - Teardown the configured OB ATU
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 * @mw: Index of the memory window (either 0, 1, 2 or 3)
 *
 * Teardown the configured OB ATU configured in epf_ntb_configure_mw() using
 * pci_epc_unmap_addr()
 */
static void epf_ntb_teardown_mw(struct epf_ntb *ntb, u32 mw)
{
	pci_epc_unmap_addr(ntb->epf->epc,
			   ntb->epf->func_no,
			   ntb->epf->vfunc_no,
			   ntb->vpci_mw_phy[mw]);
}

/**
 * epf_ntb_cmd_handler() - Handle commands provided by the NTB HOST
 * @work: work_struct for the epf_ntb_epc
 *
 * Workqueue function that gets invoked for the two epf_ntb_epc
 * periodically (once every 5ms) to see if it has received any commands
 * from NTB HOST. The HOST can send commands to configure doorbell or
 * configure memory window or to update link status.
 */
static void epf_ntb_cmd_handler(struct work_struct *work)
{
	struct epf_ntb_ctrl *ctrl;
	u32 command, argument;
	struct epf_ntb *ntb;
	struct device *dev;
	int ret;
	int i;

	ntb = container_of(work, struct epf_ntb, cmd_handler.work);

	for (i = 1; i < ntb->db_count; i++) {
		if (ntb->epf_db[i]) {
			ntb->db |= 1 << (i - 1);
			ntb_db_event(&ntb->ntb, i);
			ntb->epf_db[i] = 0;
		}
	}

	ctrl = ntb->reg;
	command = ctrl->command;
	if (!command)
		goto reset_handler;
	argument = ctrl->argument;

	ctrl->command = 0;
	ctrl->argument = 0;

	ctrl = ntb->reg;
	dev = &ntb->epf->dev;

	switch (command) {
	case COMMAND_CONFIGURE_DOORBELL:
		ctrl->command_status = COMMAND_STATUS_OK;
		break;
	case COMMAND_TEARDOWN_DOORBELL:
		ctrl->command_status = COMMAND_STATUS_OK;
		break;
	case COMMAND_CONFIGURE_MW:
		ret = epf_ntb_configure_mw(ntb, argument);
		if (ret < 0)
			ctrl->command_status = COMMAND_STATUS_ERROR;
		else
			ctrl->command_status = COMMAND_STATUS_OK;
		break;
	case COMMAND_TEARDOWN_MW:
		epf_ntb_teardown_mw(ntb, argument);
		ctrl->command_status = COMMAND_STATUS_OK;
		break;
	case COMMAND_LINK_UP:
		ntb->linkup = true;
		ret = epf_ntb_link_up(ntb, true);
		if (ret < 0)
			ctrl->command_status = COMMAND_STATUS_ERROR;
		else
			ctrl->command_status = COMMAND_STATUS_OK;
		goto reset_handler;
	case COMMAND_LINK_DOWN:
		ntb->linkup = false;
		ret = epf_ntb_link_up(ntb, false);
		if (ret < 0)
			ctrl->command_status = COMMAND_STATUS_ERROR;
		else
			ctrl->command_status = COMMAND_STATUS_OK;
		break;
	default:
		dev_err(dev, "UNKNOWN command: %d\n", command);
		break;
	}

reset_handler:
	queue_delayed_work(kpcintb_workqueue, &ntb->cmd_handler,
			   msecs_to_jiffies(5));
}

/**
 * epf_ntb_config_sspad_bar_clear() - Clear Config + Self scratchpad BAR
 * @ntb: EPC associated with one of the HOST which holds peer's outbound
 *	 address.
 *
 * Clear BAR0 of EP CONTROLLER 1 which contains the HOST1's config and
 * self scratchpad region (removes inbound ATU configuration). While BAR0 is
 * the default self scratchpad BAR, an NTB could have other BARs for self
 * scratchpad (because of reserved BARs). This function can get the exact BAR
 * used for self scratchpad from epf_ntb_bar[BAR_CONFIG].
 *
 * Please note the self scratchpad region and config region is combined to
 * a single region and mapped using the same BAR. Also note VHOST's peer
 * scratchpad is HOST's self scratchpad.
 *
 * Returns: void
 */
static void epf_ntb_config_sspad_bar_clear(struct epf_ntb *ntb)
{
	struct pci_epf_bar *epf_bar;
	enum pci_barno barno;

	barno = ntb->epf_ntb_bar[BAR_CONFIG];
	epf_bar = &ntb->epf->bar[barno];

	pci_epc_clear_bar(ntb->epf->epc, ntb->epf->func_no, ntb->epf->vfunc_no, epf_bar);
}

/**
 * epf_ntb_config_sspad_bar_set() - Set Config + Self scratchpad BAR
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Map BAR0 of EP CONTROLLER which contains the VHOST's config and
 * self scratchpad region.
 *
 * Please note the self scratchpad region and config region is combined to
 * a single region and mapped using the same BAR.
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_config_sspad_bar_set(struct epf_ntb *ntb)
{
	struct pci_epf_bar *epf_bar;
	enum pci_barno barno;
	u8 func_no, vfunc_no;
	struct device *dev;
	int ret;

	dev = &ntb->epf->dev;
	func_no = ntb->epf->func_no;
	vfunc_no = ntb->epf->vfunc_no;
	barno = ntb->epf_ntb_bar[BAR_CONFIG];
	epf_bar = &ntb->epf->bar[barno];

	ret = pci_epc_set_bar(ntb->epf->epc, func_no, vfunc_no, epf_bar);
	if (ret) {
		dev_err(dev, "inft: Config/Status/SPAD BAR set failed\n");
		return ret;
	}
	return 0;
}

/**
 * epf_ntb_config_spad_bar_free() - Free the physical memory associated with
 *   config + scratchpad region
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 */
static void epf_ntb_config_spad_bar_free(struct epf_ntb *ntb)
{
	enum pci_barno barno;

	barno = ntb->epf_ntb_bar[BAR_CONFIG];
	pci_epf_free_space(ntb->epf, ntb->reg, barno, 0);
}

/**
 * epf_ntb_config_spad_bar_alloc() - Allocate memory for config + scratchpad
 *   region
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Allocate the Local Memory mentioned in the above diagram. The size of
 * CONFIG REGION is sizeof(struct epf_ntb_ctrl) and size of SCRATCHPAD REGION
 * is obtained from "spad-count" configfs entry.
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_config_spad_bar_alloc(struct epf_ntb *ntb)
{
	size_t align;
	enum pci_barno barno;
	struct epf_ntb_ctrl *ctrl;
	u32 spad_size, ctrl_size;
	u64 size;
	struct pci_epf *epf = ntb->epf;
	struct device *dev = &epf->dev;
	u32 spad_count;
	void *base;
	int i;
	const struct pci_epc_features *epc_features = pci_epc_get_features(epf->epc,
								epf->func_no,
								epf->vfunc_no);
	barno = ntb->epf_ntb_bar[BAR_CONFIG];
	size = epc_features->bar_fixed_size[barno];
	align = epc_features->align;

	if ((!IS_ALIGNED(size, align)))
		return -EINVAL;

	spad_count = ntb->spad_count;

	ctrl_size = sizeof(struct epf_ntb_ctrl);
	spad_size = 2 * spad_count * sizeof(u32);

	if (!align) {
		ctrl_size = roundup_pow_of_two(ctrl_size);
		spad_size = roundup_pow_of_two(spad_size);
	} else {
		ctrl_size = ALIGN(ctrl_size, align);
		spad_size = ALIGN(spad_size, align);
	}

	if (!size)
		size = ctrl_size + spad_size;
	else if (size < ctrl_size + spad_size)
		return -EINVAL;

	base = pci_epf_alloc_space(epf, size, barno, align, 0);
	if (!base) {
		dev_err(dev, "Config/Status/SPAD alloc region fail\n");
		return -ENOMEM;
	}

	ntb->reg = base;

	ctrl = ntb->reg;
	ctrl->spad_offset = ctrl_size;

	ctrl->spad_count = spad_count;
	ctrl->num_mws = ntb->num_mws;
	ntb->spad_size = spad_size;

	ctrl->db_entry_size = sizeof(u32);

	for (i = 0; i < ntb->db_count; i++) {
		ntb->reg->db_data[i] = 1 + i;
		ntb->reg->db_offset[i] = 0;
	}

	return 0;
}

/**
 * epf_ntb_configure_interrupt() - Configure MSI/MSI-X capability
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Configure MSI/MSI-X capability for each interface with number of
 * interrupts equal to "db_count" configfs entry.
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_configure_interrupt(struct epf_ntb *ntb)
{
	const struct pci_epc_features *epc_features;
	struct device *dev;
	u32 db_count;
	int ret;

	dev = &ntb->epf->dev;

	epc_features = pci_epc_get_features(ntb->epf->epc, ntb->epf->func_no, ntb->epf->vfunc_no);

	if (!(epc_features->msix_capable || epc_features->msi_capable)) {
		dev_err(dev, "MSI or MSI-X is required for doorbell\n");
		return -EINVAL;
	}

	db_count = ntb->db_count;
	if (db_count > MAX_DB_COUNT) {
		dev_err(dev, "DB count cannot be more than %d\n", MAX_DB_COUNT);
		return -EINVAL;
	}

	ntb->db_count = db_count;

	if (epc_features->msi_capable) {
		ret = pci_epc_set_msi(ntb->epf->epc,
				      ntb->epf->func_no,
				      ntb->epf->vfunc_no,
				      16);
		if (ret) {
			dev_err(dev, "MSI configuration failed\n");
			return ret;
		}
	}

	return 0;
}

/**
 * epf_ntb_db_bar_init() - Configure Doorbell window BARs
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_db_bar_init(struct epf_ntb *ntb)
{
	const struct pci_epc_features *epc_features;
	u32 align;
	struct device *dev = &ntb->epf->dev;
	int ret;
	struct pci_epf_bar *epf_bar;
	void __iomem *mw_addr;
	enum pci_barno barno;
	size_t size = sizeof(u32) * ntb->db_count;

	epc_features = pci_epc_get_features(ntb->epf->epc,
					    ntb->epf->func_no,
					    ntb->epf->vfunc_no);
	align = epc_features->align;

	if (size < 128)
		size = 128;

	if (align)
		size = ALIGN(size, align);
	else
		size = roundup_pow_of_two(size);

	barno = ntb->epf_ntb_bar[BAR_DB];

	mw_addr = pci_epf_alloc_space(ntb->epf, size, barno, align, 0);
	if (!mw_addr) {
		dev_err(dev, "Failed to allocate OB address\n");
		return -ENOMEM;
	}

	ntb->epf_db = mw_addr;

	epf_bar = &ntb->epf->bar[barno];

	ret = pci_epc_set_bar(ntb->epf->epc, ntb->epf->func_no, ntb->epf->vfunc_no, epf_bar);
	if (ret) {
		dev_err(dev, "Doorbell BAR set failed\n");
			goto err_alloc_peer_mem;
	}
	return ret;

err_alloc_peer_mem:
	pci_epf_free_space(ntb->epf, mw_addr, barno, 0);
	return -1;
}

static void epf_ntb_mw_bar_clear(struct epf_ntb *ntb, int num_mws);

/**
 * epf_ntb_db_bar_clear() - Clear doorbell BAR and free memory
 *   allocated in peer's outbound address space
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 */
static void epf_ntb_db_bar_clear(struct epf_ntb *ntb)
{
	enum pci_barno barno;

	barno = ntb->epf_ntb_bar[BAR_DB];
	pci_epf_free_space(ntb->epf, ntb->epf_db, barno, 0);
	pci_epc_clear_bar(ntb->epf->epc,
			  ntb->epf->func_no,
			  ntb->epf->vfunc_no,
			  &ntb->epf->bar[barno]);
}

/**
 * epf_ntb_mw_bar_init() - Configure Memory window BARs
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_mw_bar_init(struct epf_ntb *ntb)
{
	int ret = 0;
	int i;
	u64 size;
	enum pci_barno barno;
	struct device *dev = &ntb->epf->dev;

	for (i = 0; i < ntb->num_mws; i++) {
		size = ntb->mws_size[i];
		barno = ntb->epf_ntb_bar[BAR_MW0 + i];

		ntb->epf->bar[barno].barno = barno;
		ntb->epf->bar[barno].size = size;
		ntb->epf->bar[barno].addr = NULL;
		ntb->epf->bar[barno].phys_addr = 0;
		ntb->epf->bar[barno].flags |= upper_32_bits(size) ?
				PCI_BASE_ADDRESS_MEM_TYPE_64 :
				PCI_BASE_ADDRESS_MEM_TYPE_32;

		ret = pci_epc_set_bar(ntb->epf->epc,
				      ntb->epf->func_no,
				      ntb->epf->vfunc_no,
				      &ntb->epf->bar[barno]);
		if (ret) {
			dev_err(dev, "MW set failed\n");
			goto err_alloc_mem;
		}

		/* Allocate EPC outbound memory windows to vpci vntb device */
		ntb->vpci_mw_addr[i] = pci_epc_mem_alloc_addr(ntb->epf->epc,
							      &ntb->vpci_mw_phy[i],
							      size);
		if (!ntb->vpci_mw_addr[i]) {
			ret = -ENOMEM;
			dev_err(dev, "Failed to allocate source address\n");
			goto err_set_bar;
		}
	}

	return ret;

err_set_bar:
	pci_epc_clear_bar(ntb->epf->epc,
			  ntb->epf->func_no,
			  ntb->epf->vfunc_no,
			  &ntb->epf->bar[barno]);
err_alloc_mem:
	epf_ntb_mw_bar_clear(ntb, i);
	return ret;
}

/**
 * epf_ntb_mw_bar_clear() - Clear Memory window BARs
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 * @num_mws: the number of Memory window BARs that to be cleared
 */
static void epf_ntb_mw_bar_clear(struct epf_ntb *ntb, int num_mws)
{
	enum pci_barno barno;
	int i;

	for (i = 0; i < num_mws; i++) {
		barno = ntb->epf_ntb_bar[BAR_MW0 + i];
		pci_epc_clear_bar(ntb->epf->epc,
				  ntb->epf->func_no,
				  ntb->epf->vfunc_no,
				  &ntb->epf->bar[barno]);

		pci_epc_mem_free_addr(ntb->epf->epc,
				      ntb->vpci_mw_phy[i],
				      ntb->vpci_mw_addr[i],
				      ntb->mws_size[i]);
	}
}

/**
 * epf_ntb_epc_destroy() - Cleanup NTB EPC interface
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Wrapper for epf_ntb_epc_destroy_interface() to cleanup all the NTB interfaces
 */
static void epf_ntb_epc_destroy(struct epf_ntb *ntb)
{
	pci_epc_remove_epf(ntb->epf->epc, ntb->epf, 0);
	pci_epc_put(ntb->epf->epc);
}

/**
 * epf_ntb_init_epc_bar() - Identify BARs to be used for each of the NTB
 * constructs (scratchpad region, doorbell, memorywindow)
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_init_epc_bar(struct epf_ntb *ntb)
{
	const struct pci_epc_features *epc_features;
	enum pci_barno barno;
	enum epf_ntb_bar bar;
	struct device *dev;
	u32 num_mws;
	int i;

	barno = BAR_0;
	num_mws = ntb->num_mws;
	dev = &ntb->epf->dev;
	epc_features = pci_epc_get_features(ntb->epf->epc, ntb->epf->func_no, ntb->epf->vfunc_no);

	/* These are required BARs which are mandatory for NTB functionality */
	for (bar = BAR_CONFIG; bar <= BAR_MW0; bar++, barno++) {
		barno = pci_epc_get_next_free_bar(epc_features, barno);
		if (barno < 0) {
			dev_err(dev, "Fail to get NTB function BAR\n");
			return barno;
		}
		ntb->epf_ntb_bar[bar] = barno;
	}

	/* These are optional BARs which don't impact NTB functionality */
	for (bar = BAR_MW1, i = 1; i < num_mws; bar++, barno++, i++) {
		barno = pci_epc_get_next_free_bar(epc_features, barno);
		if (barno < 0) {
			ntb->num_mws = i;
			dev_dbg(dev, "BAR not available for > MW%d\n", i + 1);
		}
		ntb->epf_ntb_bar[bar] = barno;
	}

	return 0;
}

/**
 * epf_ntb_epc_init() - Initialize NTB interface
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Wrapper to initialize a particular EPC interface and start the workqueue
 * to check for commands from HOST. This function will write to the
 * EP controller HW for configuring it.
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_epc_init(struct epf_ntb *ntb)
{
	u8 func_no, vfunc_no;
	struct pci_epc *epc;
	struct pci_epf *epf;
	struct device *dev;
	int ret;

	epf = ntb->epf;
	dev = &epf->dev;
	epc = epf->epc;
	func_no = ntb->epf->func_no;
	vfunc_no = ntb->epf->vfunc_no;

	ret = epf_ntb_config_sspad_bar_set(ntb);
	if (ret) {
		dev_err(dev, "Config/self SPAD BAR init failed");
		return ret;
	}

	ret = epf_ntb_configure_interrupt(ntb);
	if (ret) {
		dev_err(dev, "Interrupt configuration failed\n");
		goto err_config_interrupt;
	}

	ret = epf_ntb_db_bar_init(ntb);
	if (ret) {
		dev_err(dev, "DB BAR init failed\n");
		goto err_db_bar_init;
	}

	ret = epf_ntb_mw_bar_init(ntb);
	if (ret) {
		dev_err(dev, "MW BAR init failed\n");
		goto err_mw_bar_init;
	}

	if (vfunc_no <= 1) {
		ret = pci_epc_write_header(epc, func_no, vfunc_no, epf->header);
		if (ret) {
			dev_err(dev, "Configuration header write failed\n");
			goto err_write_header;
		}
	}

	INIT_DELAYED_WORK(&ntb->cmd_handler, epf_ntb_cmd_handler);
	queue_work(kpcintb_workqueue, &ntb->cmd_handler.work);

	return 0;

err_write_header:
	epf_ntb_mw_bar_clear(ntb, ntb->num_mws);
err_mw_bar_init:
	epf_ntb_db_bar_clear(ntb);
err_db_bar_init:
err_config_interrupt:
	epf_ntb_config_sspad_bar_clear(ntb);

	return ret;
}


/**
 * epf_ntb_epc_cleanup() - Cleanup all NTB interfaces
 * @ntb: NTB device that facilitates communication between HOST and VHOST
 *
 * Wrapper to cleanup all NTB interfaces.
 */
static void epf_ntb_epc_cleanup(struct epf_ntb *ntb)
{
	epf_ntb_db_bar_clear(ntb);
	epf_ntb_mw_bar_clear(ntb, ntb->num_mws);
}

#define EPF_NTB_R(_name)						\
static ssize_t epf_ntb_##_name##_show(struct config_item *item,		\
				      char *page)			\
{									\
	struct config_group *group = to_config_group(item);		\
	struct epf_ntb *ntb = to_epf_ntb(group);			\
									\
	return sprintf(page, "%d\n", ntb->_name);			\
}

#define EPF_NTB_W(_name)						\
static ssize_t epf_ntb_##_name##_store(struct config_item *item,	\
				       const char *page, size_t len)	\
{									\
	struct config_group *group = to_config_group(item);		\
	struct epf_ntb *ntb = to_epf_ntb(group);			\
	u32 val;							\
	int ret;							\
									\
	ret = kstrtou32(page, 0, &val);					\
	if (ret)							\
		return ret;						\
									\
	ntb->_name = val;						\
									\
	return len;							\
}

#define EPF_NTB_MW_R(_name)						\
static ssize_t epf_ntb_##_name##_show(struct config_item *item,		\
				      char *page)			\
{									\
	struct config_group *group = to_config_group(item);		\
	struct epf_ntb *ntb = to_epf_ntb(group);			\
	struct device *dev = &ntb->epf->dev;				\
	int win_no;							\
									\
	if (sscanf(#_name, "mw%d", &win_no) != 1)			\
		return -EINVAL;						\
									\
	if (win_no <= 0 || win_no > ntb->num_mws) {			\
		dev_err(dev, "Invalid num_nws: %d value\n", ntb->num_mws); \
		return -EINVAL;						\
	}								\
									\
	return sprintf(page, "%lld\n", ntb->mws_size[win_no - 1]);	\
}

#define EPF_NTB_MW_W(_name)						\
static ssize_t epf_ntb_##_name##_store(struct config_item *item,	\
				       const char *page, size_t len)	\
{									\
	struct config_group *group = to_config_group(item);		\
	struct epf_ntb *ntb = to_epf_ntb(group);			\
	struct device *dev = &ntb->epf->dev;				\
	int win_no;							\
	u64 val;							\
	int ret;							\
									\
	ret = kstrtou64(page, 0, &val);					\
	if (ret)							\
		return ret;						\
									\
	if (sscanf(#_name, "mw%d", &win_no) != 1)			\
		return -EINVAL;						\
									\
	if (win_no <= 0 || win_no > ntb->num_mws) {			\
		dev_err(dev, "Invalid num_nws: %d value\n", ntb->num_mws); \
		return -EINVAL;						\
	}								\
									\
	ntb->mws_size[win_no - 1] = val;				\
									\
	return len;							\
}

static ssize_t epf_ntb_num_mws_store(struct config_item *item,
				     const char *page, size_t len)
{
	struct config_group *group = to_config_group(item);
	struct epf_ntb *ntb = to_epf_ntb(group);
	u32 val;
	int ret;

	ret = kstrtou32(page, 0, &val);
	if (ret)
		return ret;

	if (val > MAX_MW)
		return -EINVAL;

	ntb->num_mws = val;

	return len;
}

EPF_NTB_R(spad_count)
EPF_NTB_W(spad_count)
EPF_NTB_R(db_count)
EPF_NTB_W(db_count)
EPF_NTB_R(num_mws)
EPF_NTB_R(vbus_number)
EPF_NTB_W(vbus_number)
EPF_NTB_R(vntb_pid)
EPF_NTB_W(vntb_pid)
EPF_NTB_R(vntb_vid)
EPF_NTB_W(vntb_vid)
EPF_NTB_MW_R(mw1)
EPF_NTB_MW_W(mw1)
EPF_NTB_MW_R(mw2)
EPF_NTB_MW_W(mw2)
EPF_NTB_MW_R(mw3)
EPF_NTB_MW_W(mw3)
EPF_NTB_MW_R(mw4)
EPF_NTB_MW_W(mw4)

CONFIGFS_ATTR(epf_ntb_, spad_count);
CONFIGFS_ATTR(epf_ntb_, db_count);
CONFIGFS_ATTR(epf_ntb_, num_mws);
CONFIGFS_ATTR(epf_ntb_, mw1);
CONFIGFS_ATTR(epf_ntb_, mw2);
CONFIGFS_ATTR(epf_ntb_, mw3);
CONFIGFS_ATTR(epf_ntb_, mw4);
CONFIGFS_ATTR(epf_ntb_, vbus_number);
CONFIGFS_ATTR(epf_ntb_, vntb_pid);
CONFIGFS_ATTR(epf_ntb_, vntb_vid);

static struct configfs_attribute *epf_ntb_attrs[] = {
	&epf_ntb_attr_spad_count,
	&epf_ntb_attr_db_count,
	&epf_ntb_attr_num_mws,
	&epf_ntb_attr_mw1,
	&epf_ntb_attr_mw2,
	&epf_ntb_attr_mw3,
	&epf_ntb_attr_mw4,
	&epf_ntb_attr_vbus_number,
	&epf_ntb_attr_vntb_pid,
	&epf_ntb_attr_vntb_vid,
	NULL,
};

static const struct config_item_type ntb_group_type = {
	.ct_attrs	= epf_ntb_attrs,
	.ct_owner	= THIS_MODULE,
};

/**
 * epf_ntb_add_cfs() - Add configfs directory specific to NTB
 * @epf: NTB endpoint function device
 * @group: A pointer to the config_group structure referencing a group of
 *	   config_items of a specific type that belong to a specific sub-system.
 *
 * Add configfs directory specific to NTB. This directory will hold
 * NTB specific properties like db_count, spad_count, num_mws etc.,
 *
 * Returns: Pointer to config_group
 */
static struct config_group *epf_ntb_add_cfs(struct pci_epf *epf,
					    struct config_group *group)
{
	struct epf_ntb *ntb = epf_get_drvdata(epf);
	struct config_group *ntb_group = &ntb->group;
	struct device *dev = &epf->dev;

	config_group_init_type_name(ntb_group, dev_name(dev), &ntb_group_type);

	return ntb_group;
}

/*==== virtual PCI bus driver, which only load virtual NTB PCI driver ====*/

static u32 pci_space[] = {
	0xffffffff,	/*DeviceID, Vendor ID*/
	0,		/*Status, Command*/
	0xffffffff,	/*Class code, subclass, prog if, revision id*/
	0x40,		/*bist, header type, latency Timer, cache line size*/
	0,		/*BAR 0*/
	0,		/*BAR 1*/
	0,		/*BAR 2*/
	0,		/*BAR 3*/
	0,		/*BAR 4*/
	0,		/*BAR 5*/
	0,		/*Cardbus cis point*/
	0,		/*Subsystem ID Subystem vendor id*/
	0,		/*ROM Base Address*/
	0,		/*Reserved, Cap. Point*/
	0,		/*Reserved,*/
	0,		/*Max Lat, Min Gnt, interrupt pin, interrupt line*/
};

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	if (devfn == 0) {
		memcpy(val, ((u8 *)pci_space) + where, size);
		return PCIBIOS_SUCCESSFUL;
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	return 0;
}

static struct pci_ops vpci_ops = {
	.read = pci_read,
	.write = pci_write,
};

static int vpci_scan_bus(void *sysdata)
{
	struct pci_bus *vpci_bus;
	struct epf_ntb *ndev = sysdata;

	vpci_bus = pci_scan_bus(ndev->vbus_number, &vpci_ops, sysdata);
	if (vpci_bus)
		pr_err("create pci bus\n");

	pci_bus_add_devices(vpci_bus);

	return 0;
}

/*==================== Virtual PCIe NTB driver ==========================*/

static int vntb_epf_mw_count(struct ntb_dev *ntb, int pidx)
{
	struct epf_ntb *ndev = ntb_ndev(ntb);

	return ndev->num_mws;
}

static int vntb_epf_spad_count(struct ntb_dev *ntb)
{
	return ntb_ndev(ntb)->spad_count;
}

static int vntb_epf_peer_mw_count(struct ntb_dev *ntb)
{
	return ntb_ndev(ntb)->num_mws;
}

static u64 vntb_epf_db_valid_mask(struct ntb_dev *ntb)
{
	return BIT_ULL(ntb_ndev(ntb)->db_count) - 1;
}

static int vntb_epf_db_set_mask(struct ntb_dev *ntb, u64 db_bits)
{
	return 0;
}

static int vntb_epf_mw_set_trans(struct ntb_dev *ndev, int pidx, int idx,
		dma_addr_t addr, resource_size_t size)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);
	struct pci_epf_bar *epf_bar;
	enum pci_barno barno;
	int ret;
	struct device *dev;

	dev = &ntb->ntb.dev;
	barno = ntb->epf_ntb_bar[BAR_MW0 + idx];
	epf_bar = &ntb->epf->bar[barno];
	epf_bar->phys_addr = addr;
	epf_bar->barno = barno;
	epf_bar->size = size;

	ret = pci_epc_set_bar(ntb->epf->epc, 0, 0, epf_bar);
	if (ret) {
		dev_err(dev, "failure set mw trans\n");
		return ret;
	}
	return 0;
}

static int vntb_epf_mw_clear_trans(struct ntb_dev *ntb, int pidx, int idx)
{
	return 0;
}

static int vntb_epf_peer_mw_get_addr(struct ntb_dev *ndev, int idx,
				phys_addr_t *base, resource_size_t *size)
{

	struct epf_ntb *ntb = ntb_ndev(ndev);

	if (base)
		*base = ntb->vpci_mw_phy[idx];

	if (size)
		*size = ntb->mws_size[idx];

	return 0;
}

static int vntb_epf_link_enable(struct ntb_dev *ntb,
			enum ntb_speed max_speed,
			enum ntb_width max_width)
{
	return 0;
}

static u32 vntb_epf_spad_read(struct ntb_dev *ndev, int idx)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);
	int off = ntb->reg->spad_offset, ct = ntb->reg->spad_count * sizeof(u32);
	u32 val;
	void __iomem *base = (void __iomem *)ntb->reg;

	val = readl(base + off + ct + idx * sizeof(u32));
	return val;
}

static int vntb_epf_spad_write(struct ntb_dev *ndev, int idx, u32 val)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);
	struct epf_ntb_ctrl *ctrl = ntb->reg;
	int off = ctrl->spad_offset, ct = ctrl->spad_count * sizeof(u32);
	void __iomem *base = (void __iomem *)ntb->reg;

	writel(val, base + off + ct + idx * sizeof(u32));
	return 0;
}

static u32 vntb_epf_peer_spad_read(struct ntb_dev *ndev, int pidx, int idx)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);
	struct epf_ntb_ctrl *ctrl = ntb->reg;
	int off = ctrl->spad_offset;
	void __iomem *base = (void __iomem *)ntb->reg;
	u32 val;

	val = readl(base + off + idx * sizeof(u32));
	return val;
}

static int vntb_epf_peer_spad_write(struct ntb_dev *ndev, int pidx, int idx, u32 val)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);
	struct epf_ntb_ctrl *ctrl = ntb->reg;
	int off = ctrl->spad_offset;
	void __iomem *base = (void __iomem *)ntb->reg;

	writel(val, base + off + idx * sizeof(u32));
	return 0;
}

static int vntb_epf_peer_db_set(struct ntb_dev *ndev, u64 db_bits)
{
	u32 interrupt_num = ffs(db_bits) + 1;
	struct epf_ntb *ntb = ntb_ndev(ndev);
	u8 func_no, vfunc_no;
	int ret;

	func_no = ntb->epf->func_no;
	vfunc_no = ntb->epf->vfunc_no;

	ret = pci_epc_raise_irq(ntb->epf->epc,
				func_no,
				vfunc_no,
				PCI_EPC_IRQ_MSI,
				interrupt_num + 1);
	if (ret)
		dev_err(&ntb->ntb.dev, "Failed to raise IRQ\n");

	return ret;
}

static u64 vntb_epf_db_read(struct ntb_dev *ndev)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);

	return ntb->db;
}

static int vntb_epf_mw_get_align(struct ntb_dev *ndev, int pidx, int idx,
			resource_size_t *addr_align,
			resource_size_t *size_align,
			resource_size_t *size_max)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);

	if (addr_align)
		*addr_align = SZ_4K;

	if (size_align)
		*size_align = 1;

	if (size_max)
		*size_max = ntb->mws_size[idx];

	return 0;
}

static u64 vntb_epf_link_is_up(struct ntb_dev *ndev,
			enum ntb_speed *speed,
			enum ntb_width *width)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);

	return ntb->reg->link_status;
}

static int vntb_epf_db_clear_mask(struct ntb_dev *ndev, u64 db_bits)
{
	return 0;
}

static int vntb_epf_db_clear(struct ntb_dev *ndev, u64 db_bits)
{
	struct epf_ntb *ntb = ntb_ndev(ndev);

	ntb->db &= ~db_bits;
	return 0;
}

static int vntb_epf_link_disable(struct ntb_dev *ntb)
{
	return 0;
}

static const struct ntb_dev_ops vntb_epf_ops = {
	.mw_count		= vntb_epf_mw_count,
	.spad_count		= vntb_epf_spad_count,
	.peer_mw_count		= vntb_epf_peer_mw_count,
	.db_valid_mask		= vntb_epf_db_valid_mask,
	.db_set_mask		= vntb_epf_db_set_mask,
	.mw_set_trans		= vntb_epf_mw_set_trans,
	.mw_clear_trans		= vntb_epf_mw_clear_trans,
	.peer_mw_get_addr	= vntb_epf_peer_mw_get_addr,
	.link_enable		= vntb_epf_link_enable,
	.spad_read		= vntb_epf_spad_read,
	.spad_write		= vntb_epf_spad_write,
	.peer_spad_read		= vntb_epf_peer_spad_read,
	.peer_spad_write	= vntb_epf_peer_spad_write,
	.peer_db_set		= vntb_epf_peer_db_set,
	.db_read		= vntb_epf_db_read,
	.mw_get_align		= vntb_epf_mw_get_align,
	.link_is_up		= vntb_epf_link_is_up,
	.db_clear_mask		= vntb_epf_db_clear_mask,
	.db_clear		= vntb_epf_db_clear,
	.link_disable		= vntb_epf_link_disable,
};

static int pci_vntb_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	struct epf_ntb *ndev = (struct epf_ntb *)pdev->sysdata;
	struct device *dev = &pdev->dev;

	ndev->ntb.pdev = pdev;
	ndev->ntb.topo = NTB_TOPO_NONE;
	ndev->ntb.ops =  &vntb_epf_ops;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "Cannot set DMA mask\n");
		return -EINVAL;
	}

	ret = ntb_register_device(&ndev->ntb);
	if (ret) {
		dev_err(dev, "Failed to register NTB device\n");
		goto err_register_dev;
	}

	dev_dbg(dev, "PCI Virtual NTB driver loaded\n");
	return 0;

err_register_dev:
	return -EINVAL;
}

static struct pci_device_id pci_vntb_table[] = {
	{
		PCI_DEVICE(0xffff, 0xffff),
	},
	{},
};

static struct pci_driver vntb_pci_driver = {
	.name           = "pci-vntb",
	.id_table       = pci_vntb_table,
	.probe          = pci_vntb_probe,
};

/* ============ PCIe EPF Driver Bind ====================*/

/**
 * epf_ntb_bind() - Initialize endpoint controller to provide NTB functionality
 * @epf: NTB endpoint function device
 *
 * Initialize both the endpoint controllers associated with NTB function device.
 * Invoked when a primary interface or secondary interface is bound to EPC
 * device. This function will succeed only when EPC is bound to both the
 * interfaces.
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_bind(struct pci_epf *epf)
{
	struct epf_ntb *ntb = epf_get_drvdata(epf);
	struct device *dev = &epf->dev;
	int ret;

	if (!epf->epc) {
		dev_dbg(dev, "PRIMARY EPC interface not yet bound\n");
		return 0;
	}

	ret = epf_ntb_init_epc_bar(ntb);
	if (ret) {
		dev_err(dev, "Failed to create NTB EPC\n");
		goto err_bar_init;
	}

	ret = epf_ntb_config_spad_bar_alloc(ntb);
	if (ret) {
		dev_err(dev, "Failed to allocate BAR memory\n");
		goto err_bar_alloc;
	}

	ret = epf_ntb_epc_init(ntb);
	if (ret) {
		dev_err(dev, "Failed to initialize EPC\n");
		goto err_bar_alloc;
	}

	epf_set_drvdata(epf, ntb);

	pci_space[0] = (ntb->vntb_pid << 16) | ntb->vntb_vid;
	pci_vntb_table[0].vendor = ntb->vntb_vid;
	pci_vntb_table[0].device = ntb->vntb_pid;

	ret = pci_register_driver(&vntb_pci_driver);
	if (ret) {
		dev_err(dev, "failure register vntb pci driver\n");
		goto err_bar_alloc;
	}

	vpci_scan_bus(ntb);

	return 0;

err_bar_alloc:
	epf_ntb_config_spad_bar_free(ntb);

err_bar_init:
	epf_ntb_epc_destroy(ntb);

	return ret;
}

/**
 * epf_ntb_unbind() - Cleanup the initialization from epf_ntb_bind()
 * @epf: NTB endpoint function device
 *
 * Cleanup the initialization from epf_ntb_bind()
 */
static void epf_ntb_unbind(struct pci_epf *epf)
{
	struct epf_ntb *ntb = epf_get_drvdata(epf);

	epf_ntb_epc_cleanup(ntb);
	epf_ntb_config_spad_bar_free(ntb);
	epf_ntb_epc_destroy(ntb);

	pci_unregister_driver(&vntb_pci_driver);
}

// EPF driver probe
static struct pci_epf_ops epf_ntb_ops = {
	.bind   = epf_ntb_bind,
	.unbind = epf_ntb_unbind,
	.add_cfs = epf_ntb_add_cfs,
};

/**
 * epf_ntb_probe() - Probe NTB function driver
 * @epf: NTB endpoint function device
 *
 * Probe NTB function driver when endpoint function bus detects a NTB
 * endpoint function.
 *
 * Returns: Zero for success, or an error code in case of failure
 */
static int epf_ntb_probe(struct pci_epf *epf)
{
	struct epf_ntb *ntb;
	struct device *dev;

	dev = &epf->dev;

	ntb = devm_kzalloc(dev, sizeof(*ntb), GFP_KERNEL);
	if (!ntb)
		return -ENOMEM;

	epf->header = &epf_ntb_header;
	ntb->epf = epf;
	ntb->vbus_number = 0xff;
	epf_set_drvdata(epf, ntb);

	dev_info(dev, "pci-ep epf driver loaded\n");
	return 0;
}

static const struct pci_epf_device_id epf_ntb_ids[] = {
	{
		.name = "pci_epf_vntb",
	},
	{},
};

static struct pci_epf_driver epf_ntb_driver = {
	.driver.name    = "pci_epf_vntb",
	.probe          = epf_ntb_probe,
	.id_table       = epf_ntb_ids,
	.ops            = &epf_ntb_ops,
	.owner          = THIS_MODULE,
};

static int __init epf_ntb_init(void)
{
	int ret;

	kpcintb_workqueue = alloc_workqueue("kpcintb", WQ_MEM_RECLAIM |
					    WQ_HIGHPRI, 0);
	ret = pci_epf_register_driver(&epf_ntb_driver);
	if (ret) {
		destroy_workqueue(kpcintb_workqueue);
		pr_err("Failed to register pci epf ntb driver --> %d\n", ret);
		return ret;
	}

	return 0;
}
module_init(epf_ntb_init);

static void __exit epf_ntb_exit(void)
{
	pci_epf_unregister_driver(&epf_ntb_driver);
	destroy_workqueue(kpcintb_workqueue);
}
module_exit(epf_ntb_exit);

MODULE_DESCRIPTION("PCI EPF NTB DRIVER");
MODULE_AUTHOR("Frank Li <Frank.li@nxp.com>");
MODULE_LICENSE("GPL v2");

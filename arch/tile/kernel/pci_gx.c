/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/byteorder.h>

#include <gxio/iorpc_globals.h>
#include <gxio/kiorpc.h>
#include <gxio/trio.h>
#include <gxio/iorpc_trio.h>
#include <hv/drv_trio_intf.h>

#include <arch/sim.h>

/*
 * This file contains the routines to search for PCI buses,
 * enumerate the buses, and configure any attached devices.
 */

#define DEBUG_PCI_CFG	0

#if DEBUG_PCI_CFG
#define TRACE_CFG_WR(size, val, bus, dev, func, offset) \
	pr_info("CFG WR %d-byte VAL %#x to bus %d dev %d func %d addr %u\n", \
		size, val, bus, dev, func, offset & 0xFFF);
#define TRACE_CFG_RD(size, val, bus, dev, func, offset) \
	pr_info("CFG RD %d-byte VAL %#x from bus %d dev %d func %d addr %u\n", \
		size, val, bus, dev, func, offset & 0xFFF);
#else
#define TRACE_CFG_WR(...)
#define TRACE_CFG_RD(...)
#endif

static int pci_probe = 1;

/* Information on the PCIe RC ports configuration. */
static int pcie_rc[TILEGX_NUM_TRIO][TILEGX_TRIO_PCIES];

/*
 * On some platforms with one or more Gx endpoint ports, we need to
 * delay the PCIe RC port probe for a few seconds to work around
 * a HW PCIe link-training bug. The exact delay is specified with
 * a kernel boot argument in the form of "pcie_rc_delay=T,P,S",
 * where T is the TRIO instance number, P is the port number and S is
 * the delay in seconds. If the argument is specified, but the delay is
 * not provided, the value will be DEFAULT_RC_DELAY.
 */
static int rc_delay[TILEGX_NUM_TRIO][TILEGX_TRIO_PCIES];

/* Default number of seconds that the PCIe RC port probe can be delayed. */
#define DEFAULT_RC_DELAY	10

/* The PCI I/O space size in each PCI domain. */
#define IO_SPACE_SIZE		0x10000

/* Provide shorter versions of some very long constant names. */
#define AUTO_CONFIG_RC	\
	TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC
#define AUTO_CONFIG_RC_G1	\
	TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_RC_G1
#define AUTO_CONFIG_EP	\
	TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT
#define AUTO_CONFIG_EP_G1	\
	TRIO_PCIE_INTFC_PORT_CONFIG__STRAP_STATE_VAL_AUTO_CONFIG_ENDPOINT_G1

/* Array of the PCIe ports configuration info obtained from the BIB. */
struct pcie_trio_ports_property pcie_ports[TILEGX_NUM_TRIO];

/* Number of configured TRIO instances. */
int num_trio_shims;

/* All drivers share the TRIO contexts defined here. */
gxio_trio_context_t trio_contexts[TILEGX_NUM_TRIO];

/* Pointer to an array of PCIe RC controllers. */
struct pci_controller pci_controllers[TILEGX_NUM_TRIO * TILEGX_TRIO_PCIES];
int num_rc_controllers;

static struct pci_ops tile_cfg_ops;

/* Mask of CPUs that should receive PCIe interrupts. */
static struct cpumask intr_cpus_map;

/* We don't need to worry about the alignment of resources. */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				       resource_size_t size,
				       resource_size_t align)
{
	return res->start;
}
EXPORT_SYMBOL(pcibios_align_resource);

/*
 * Pick a CPU to receive and handle the PCIe interrupts, based on the IRQ #.
 * For now, we simply send interrupts to non-dataplane CPUs.
 * We may implement methods to allow user to specify the target CPUs,
 * e.g. via boot arguments.
 */
static int tile_irq_cpu(int irq)
{
	unsigned int count;
	int i = 0;
	int cpu;

	count = cpumask_weight(&intr_cpus_map);
	if (unlikely(count == 0)) {
		pr_warn("intr_cpus_map empty, interrupts will be delievered to dataplane tiles\n");
		return irq % (smp_height * smp_width);
	}

	count = irq % count;
	for_each_cpu(cpu, &intr_cpus_map) {
		if (i++ == count)
			break;
	}
	return cpu;
}

/* Open a file descriptor to the TRIO shim. */
static int tile_pcie_open(int trio_index)
{
	gxio_trio_context_t *context = &trio_contexts[trio_index];
	int ret;
	int mac;

	/* This opens a file descriptor to the TRIO shim. */
	ret = gxio_trio_init(context, trio_index);
	if (ret < 0)
		goto gxio_trio_init_failure;

	/* Allocate an ASID for the kernel. */
	ret = gxio_trio_alloc_asids(context, 1, 0, 0);
	if (ret < 0) {
		pr_err("PCI: ASID alloc failure on TRIO %d, give up\n",
			trio_index);
		goto asid_alloc_failure;
	}

	context->asid = ret;

#ifdef USE_SHARED_PCIE_CONFIG_REGION
	/*
	 * Alloc a PIO region for config access, shared by all MACs per TRIO.
	 * This shouldn't fail since the kernel is supposed to the first
	 * client of the TRIO's PIO regions.
	 */
	ret = gxio_trio_alloc_pio_regions(context, 1, 0, 0);
	if (ret < 0) {
		pr_err("PCI: CFG PIO alloc failure on TRIO %d, give up\n",
			trio_index);
		goto pio_alloc_failure;
	}

	context->pio_cfg_index = ret;

	/*
	 * For PIO CFG, the bus_address_hi parameter is 0. The mac parameter
	 * is also 0 because it is specified in PIO_REGION_SETUP_CFG_ADDR.
	 */
	ret = gxio_trio_init_pio_region_aux(context, context->pio_cfg_index,
		0, 0, HV_TRIO_PIO_FLAG_CONFIG_SPACE);
	if (ret < 0) {
		pr_err("PCI: CFG PIO init failure on TRIO %d, give up\n",
			trio_index);
		goto pio_alloc_failure;
	}
#endif

	/* Get the properties of the PCIe ports on this TRIO instance. */
	ret = gxio_trio_get_port_property(context, &pcie_ports[trio_index]);
	if (ret < 0) {
		pr_err("PCI: PCIE_GET_PORT_PROPERTY failure, error %d, on TRIO %d\n",
		       ret, trio_index);
		goto get_port_property_failure;
	}

	context->mmio_base_mac =
		iorpc_ioremap(context->fd, 0, HV_TRIO_CONFIG_IOREMAP_SIZE);
	if (context->mmio_base_mac == NULL) {
		pr_err("PCI: TRIO config space mapping failure, error %d, on TRIO %d\n",
		       ret, trio_index);
		ret = -ENOMEM;

		goto trio_mmio_mapping_failure;
	}

	/* Check the port strap state which will override the BIB setting. */
	for (mac = 0; mac < TILEGX_TRIO_PCIES; mac++) {
		TRIO_PCIE_INTFC_PORT_CONFIG_t port_config;
		unsigned int reg_offset;

		/* Ignore ports that are not specified in the BIB. */
		if (!pcie_ports[trio_index].ports[mac].allow_rc &&
		    !pcie_ports[trio_index].ports[mac].allow_ep)
			continue;

		reg_offset =
			(TRIO_PCIE_INTFC_PORT_CONFIG <<
				TRIO_CFG_REGION_ADDR__REG_SHIFT) |
			(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
				TRIO_CFG_REGION_ADDR__INTFC_SHIFT) |
			(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

		port_config.word =
			__gxio_mmio_read(context->mmio_base_mac + reg_offset);

		if (port_config.strap_state != AUTO_CONFIG_RC &&
		    port_config.strap_state != AUTO_CONFIG_RC_G1) {
			/*
			 * If this is really intended to be an EP port, record
			 * it so that the endpoint driver will know about it.
			 */
			if (port_config.strap_state == AUTO_CONFIG_EP ||
			    port_config.strap_state == AUTO_CONFIG_EP_G1)
				pcie_ports[trio_index].ports[mac].allow_ep = 1;
		}
	}

	return ret;

trio_mmio_mapping_failure:
get_port_property_failure:
asid_alloc_failure:
#ifdef USE_SHARED_PCIE_CONFIG_REGION
pio_alloc_failure:
#endif
	hv_dev_close(context->fd);
gxio_trio_init_failure:
	context->fd = -1;

	return ret;
}

static int __init tile_trio_init(void)
{
	int i;

	/* We loop over all the TRIO shims. */
	for (i = 0; i < TILEGX_NUM_TRIO; i++) {
		if (tile_pcie_open(i) < 0)
			continue;
		num_trio_shims++;
	}

	return 0;
}
postcore_initcall(tile_trio_init);

static void tilegx_legacy_irq_ack(struct irq_data *d)
{
	__insn_mtspr(SPR_IPI_EVENT_RESET_K, 1UL << d->irq);
}

static void tilegx_legacy_irq_mask(struct irq_data *d)
{
	__insn_mtspr(SPR_IPI_MASK_SET_K, 1UL << d->irq);
}

static void tilegx_legacy_irq_unmask(struct irq_data *d)
{
	__insn_mtspr(SPR_IPI_MASK_RESET_K, 1UL << d->irq);
}

static struct irq_chip tilegx_legacy_irq_chip = {
	.name			= "tilegx_legacy_irq",
	.irq_ack		= tilegx_legacy_irq_ack,
	.irq_mask		= tilegx_legacy_irq_mask,
	.irq_unmask		= tilegx_legacy_irq_unmask,

	/* TBD: support set_affinity. */
};

/*
 * This is a wrapper function of the kernel level-trigger interrupt
 * handler handle_level_irq() for PCI legacy interrupts. The TRIO
 * is configured such that only INTx Assert interrupts are proxied
 * to Linux which just calls handle_level_irq() after clearing the
 * MAC INTx Assert status bit associated with this interrupt.
 */
static void trio_handle_level_irq(struct irq_desc *desc)
{
	struct pci_controller *controller = irq_desc_get_handler_data(desc);
	gxio_trio_context_t *trio_context = controller->trio;
	uint64_t intx = (uint64_t)irq_desc_get_chip_data(desc);
	int mac = controller->mac;
	unsigned int reg_offset;
	uint64_t level_mask;

	handle_level_irq(desc);

	/*
	 * Clear the INTx Level status, otherwise future interrupts are
	 * not sent.
	 */
	reg_offset = (TRIO_PCIE_INTFC_MAC_INT_STS <<
		TRIO_CFG_REGION_ADDR__REG_SHIFT) |
		(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
		TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
		(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

	level_mask = TRIO_PCIE_INTFC_MAC_INT_STS__INT_LEVEL_MASK << intx;

	__gxio_mmio_write(trio_context->mmio_base_mac + reg_offset, level_mask);
}

/*
 * Create kernel irqs and set up the handlers for the legacy interrupts.
 * Also some minimum initialization for the MSI support.
 */
static int tile_init_irqs(struct pci_controller *controller)
{
	int i;
	int j;
	int irq;
	int result;

	cpumask_copy(&intr_cpus_map, cpu_online_mask);


	for (i = 0; i < 4; i++) {
		gxio_trio_context_t *context = controller->trio;
		int cpu;

		/* Ask the kernel to allocate an IRQ. */
		irq = irq_alloc_hwirq(-1);
		if (!irq) {
			pr_err("PCI: no free irq vectors, failed for %d\n", i);
			goto free_irqs;
		}
		controller->irq_intx_table[i] = irq;

		/* Distribute the 4 IRQs to different tiles. */
		cpu = tile_irq_cpu(irq);

		/* Configure the TRIO intr binding for this IRQ. */
		result = gxio_trio_config_legacy_intr(context, cpu_x(cpu),
						      cpu_y(cpu), KERNEL_PL,
						      irq, controller->mac, i);
		if (result < 0) {
			pr_err("PCI: MAC intx config failed for %d\n", i);

			goto free_irqs;
		}

		/* Register the IRQ handler with the kernel. */
		irq_set_chip_and_handler(irq, &tilegx_legacy_irq_chip,
					trio_handle_level_irq);
		irq_set_chip_data(irq, (void *)(uint64_t)i);
		irq_set_handler_data(irq, controller);
	}

	return 0;

free_irqs:
	for (j = 0; j < i; j++)
		irq_free_hwirq(controller->irq_intx_table[j]);

	return -1;
}

/*
 * Return 1 if the port is strapped to operate in RC mode.
 */
static int
strapped_for_rc(gxio_trio_context_t *trio_context, int mac)
{
	TRIO_PCIE_INTFC_PORT_CONFIG_t port_config;
	unsigned int reg_offset;

	/* Check the port configuration. */
	reg_offset =
		(TRIO_PCIE_INTFC_PORT_CONFIG <<
			TRIO_CFG_REGION_ADDR__REG_SHIFT) |
		(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
			TRIO_CFG_REGION_ADDR__INTFC_SHIFT) |
		(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);
	port_config.word =
		__gxio_mmio_read(trio_context->mmio_base_mac + reg_offset);

	if (port_config.strap_state == AUTO_CONFIG_RC ||
	    port_config.strap_state == AUTO_CONFIG_RC_G1)
		return 1;
	else
		return 0;
}

/*
 * Find valid controllers and fill in pci_controller structs for each
 * of them.
 *
 * Return the number of controllers discovered.
 */
int __init tile_pci_init(void)
{
	int ctl_index = 0;
	int i, j;

	if (!pci_probe) {
		pr_info("PCI: disabled by boot argument\n");
		return 0;
	}

	pr_info("PCI: Searching for controllers...\n");

	if (num_trio_shims == 0 || sim_is_simulator())
		return 0;

	/*
	 * Now determine which PCIe ports are configured to operate in RC
	 * mode. There is a difference in the port configuration capability
	 * between the Gx36 and Gx72 devices.
	 *
	 * The Gx36 has configuration capability for each of the 3 PCIe
	 * interfaces (disable, auto endpoint, auto RC, etc.).
	 * On the Gx72, you can only select one of the 3 PCIe interfaces per
	 * TRIO to train automatically. Further, the allowable training modes
	 * are reduced to four options (auto endpoint, auto RC, stream x1,
	 * stream x4).
	 *
	 * For Gx36 ports, it must be allowed to be in RC mode by the
	 * Board Information Block, and the hardware strapping pins must be
	 * set to RC mode.
	 *
	 * For Gx72 ports, the port will operate in RC mode if either of the
	 * following is true:
	 * 1. It is allowed to be in RC mode by the Board Information Block,
	 *    and the BIB doesn't allow the EP mode.
	 * 2. It is allowed to be in either the RC or the EP mode by the BIB,
	 *    and the hardware strapping pin is set to RC mode.
	 */
	for (i = 0; i < TILEGX_NUM_TRIO; i++) {
		gxio_trio_context_t *context = &trio_contexts[i];

		if (context->fd < 0)
			continue;

		for (j = 0; j < TILEGX_TRIO_PCIES; j++) {
			int is_rc = 0;

			if (pcie_ports[i].is_gx72 &&
			    pcie_ports[i].ports[j].allow_rc) {
				if (!pcie_ports[i].ports[j].allow_ep ||
				    strapped_for_rc(context, j))
					is_rc = 1;
			} else if (pcie_ports[i].ports[j].allow_rc &&
				   strapped_for_rc(context, j)) {
				is_rc = 1;
			}
			if (is_rc) {
				pcie_rc[i][j] = 1;
				num_rc_controllers++;
			}
		}
	}

	/* Return if no PCIe ports are configured to operate in RC mode. */
	if (num_rc_controllers == 0)
		return 0;

	/* Set the TRIO pointer and MAC index for each PCIe RC port. */
	for (i = 0; i < TILEGX_NUM_TRIO; i++) {
		for (j = 0; j < TILEGX_TRIO_PCIES; j++) {
			if (pcie_rc[i][j]) {
				pci_controllers[ctl_index].trio =
					&trio_contexts[i];
				pci_controllers[ctl_index].mac = j;
				pci_controllers[ctl_index].trio_index = i;
				ctl_index++;
				if (ctl_index == num_rc_controllers)
					goto out;
			}
		}
	}

out:
	/* Configure each PCIe RC port. */
	for (i = 0; i < num_rc_controllers; i++) {

		/* Configure the PCIe MAC to run in RC mode. */
		struct pci_controller *controller = &pci_controllers[i];

		controller->index = i;
		controller->ops = &tile_cfg_ops;

		controller->io_space.start = PCIBIOS_MIN_IO +
			(i * IO_SPACE_SIZE);
		controller->io_space.end = controller->io_space.start +
			IO_SPACE_SIZE - 1;
		BUG_ON(controller->io_space.end > IO_SPACE_LIMIT);
		controller->io_space.flags = IORESOURCE_IO;
		snprintf(controller->io_space_name,
			 sizeof(controller->io_space_name),
			 "PCI I/O domain %d", i);
		controller->io_space.name = controller->io_space_name;

		/*
		 * The PCI memory resource is located above the PA space.
		 * For every host bridge, the BAR window or the MMIO aperture
		 * is in range [3GB, 4GB - 1] of a 4GB space beyond the
		 * PA space.
		 */
		controller->mem_offset = TILE_PCI_MEM_START +
			(i * TILE_PCI_BAR_WINDOW_TOP);
		controller->mem_space.start = controller->mem_offset +
			TILE_PCI_BAR_WINDOW_TOP - TILE_PCI_BAR_WINDOW_SIZE;
		controller->mem_space.end = controller->mem_offset +
			TILE_PCI_BAR_WINDOW_TOP - 1;
		controller->mem_space.flags = IORESOURCE_MEM;
		snprintf(controller->mem_space_name,
			 sizeof(controller->mem_space_name),
			 "PCI mem domain %d", i);
		controller->mem_space.name = controller->mem_space_name;
	}

	return num_rc_controllers;
}

/*
 * (pin - 1) converts from the PCI standard's [1:4] convention to
 * a normal [0:3] range.
 */
static int tile_map_irq(const struct pci_dev *dev, u8 device, u8 pin)
{
	struct pci_controller *controller =
		(struct pci_controller *)dev->sysdata;
	return controller->irq_intx_table[pin - 1];
}

static void fixup_read_and_payload_sizes(struct pci_controller *controller)
{
	gxio_trio_context_t *trio_context = controller->trio;
	struct pci_bus *root_bus = controller->root_bus;
	TRIO_PCIE_RC_DEVICE_CONTROL_t dev_control;
	TRIO_PCIE_RC_DEVICE_CAP_t rc_dev_cap;
	unsigned int reg_offset;
	struct pci_bus *child;
	int mac;
	int err;

	mac = controller->mac;

	/* Set our max read request size to be 4KB. */
	reg_offset =
		(TRIO_PCIE_RC_DEVICE_CONTROL <<
			TRIO_CFG_REGION_ADDR__REG_SHIFT) |
		(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
			TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
		(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

	dev_control.word = __gxio_mmio_read32(trio_context->mmio_base_mac +
					      reg_offset);
	dev_control.max_read_req_sz = 5;
	__gxio_mmio_write32(trio_context->mmio_base_mac + reg_offset,
			    dev_control.word);

	/*
	 * Set the max payload size supported by this Gx PCIe MAC.
	 * Though Gx PCIe supports Max Payload Size of up to 1024 bytes,
	 * experiments have shown that setting MPS to 256 yields the
	 * best performance.
	 */
	reg_offset =
		(TRIO_PCIE_RC_DEVICE_CAP <<
			TRIO_CFG_REGION_ADDR__REG_SHIFT) |
		(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
			TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
		(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

	rc_dev_cap.word = __gxio_mmio_read32(trio_context->mmio_base_mac +
					     reg_offset);
	rc_dev_cap.mps_sup = 1;
	__gxio_mmio_write32(trio_context->mmio_base_mac + reg_offset,
			    rc_dev_cap.word);

	/* Configure PCI Express MPS setting. */
	list_for_each_entry(child, &root_bus->children, node)
		pcie_bus_configure_settings(child);

	/*
	 * Set the mac_config register in trio based on the MPS/MRS of the link.
	 */
	reg_offset =
		(TRIO_PCIE_RC_DEVICE_CONTROL <<
			TRIO_CFG_REGION_ADDR__REG_SHIFT) |
		(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
			TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
		(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

	dev_control.word = __gxio_mmio_read32(trio_context->mmio_base_mac +
						reg_offset);

	err = gxio_trio_set_mps_mrs(trio_context,
				    dev_control.max_payload_size,
				    dev_control.max_read_req_sz,
				    mac);
	if (err < 0) {
		pr_err("PCI: PCIE_CONFIGURE_MAC_MPS_MRS failure, MAC %d on TRIO %d\n",
		       mac, controller->trio_index);
	}
}

static int setup_pcie_rc_delay(char *str)
{
	unsigned long delay = 0;
	unsigned long trio_index;
	unsigned long mac;

	if (str == NULL || !isdigit(*str))
		return -EINVAL;
	trio_index = simple_strtoul(str, (char **)&str, 10);
	if (trio_index >= TILEGX_NUM_TRIO)
		return -EINVAL;

	if (*str != ',')
		return -EINVAL;

	str++;
	if (!isdigit(*str))
		return -EINVAL;
	mac = simple_strtoul(str, (char **)&str, 10);
	if (mac >= TILEGX_TRIO_PCIES)
		return -EINVAL;

	if (*str != '\0') {
		if (*str != ',')
			return -EINVAL;

		str++;
		if (!isdigit(*str))
			return -EINVAL;
		delay = simple_strtoul(str, (char **)&str, 10);
	}

	rc_delay[trio_index][mac] = delay ? : DEFAULT_RC_DELAY;
	return 0;
}
early_param("pcie_rc_delay", setup_pcie_rc_delay);

/* PCI initialization entry point, called by subsys_initcall. */
int __init pcibios_init(void)
{
	resource_size_t offset;
	LIST_HEAD(resources);
	int next_busno;
	int i;

	tile_pci_init();

	if (num_rc_controllers == 0)
		return 0;

	/*
	 * Delay a bit in case devices aren't ready.  Some devices are
	 * known to require at least 20ms here, but we use a more
	 * conservative value.
	 */
	msleep(250);

	/* Scan all of the recorded PCI controllers.  */
	for (next_busno = 0, i = 0; i < num_rc_controllers; i++) {
		struct pci_controller *controller = &pci_controllers[i];
		gxio_trio_context_t *trio_context = controller->trio;
		TRIO_PCIE_INTFC_PORT_STATUS_t port_status;
		TRIO_PCIE_INTFC_TX_FIFO_CTL_t tx_fifo_ctl;
		struct pci_bus *bus;
		unsigned int reg_offset;
		unsigned int class_code_revision;
		int trio_index;
		int mac;
		int ret;

		if (trio_context->fd < 0)
			continue;

		trio_index = controller->trio_index;
		mac = controller->mac;

		/*
		 * Check for PCIe link-up status to decide if we need
		 * to force the link to come up.
		 */
		reg_offset =
			(TRIO_PCIE_INTFC_PORT_STATUS <<
				TRIO_CFG_REGION_ADDR__REG_SHIFT) |
			(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
				TRIO_CFG_REGION_ADDR__INTFC_SHIFT) |
			(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

		port_status.word =
			__gxio_mmio_read(trio_context->mmio_base_mac +
					 reg_offset);
		if (!port_status.dl_up) {
			if (rc_delay[trio_index][mac]) {
				pr_info("Delaying PCIe RC TRIO init %d sec on MAC %d on TRIO %d\n",
					rc_delay[trio_index][mac], mac,
					trio_index);
				msleep(rc_delay[trio_index][mac] * 1000);
			}
			ret = gxio_trio_force_rc_link_up(trio_context, mac);
			if (ret < 0)
				pr_err("PCI: PCIE_FORCE_LINK_UP failure, MAC %d on TRIO %d\n",
				       mac, trio_index);
		}

		pr_info("PCI: Found PCI controller #%d on TRIO %d MAC %d\n",
			i, trio_index, controller->mac);

		/* Delay the bus probe if needed. */
		if (rc_delay[trio_index][mac]) {
			pr_info("Delaying PCIe RC bus enumerating %d sec on MAC %d on TRIO %d\n",
				rc_delay[trio_index][mac], mac, trio_index);
			msleep(rc_delay[trio_index][mac] * 1000);
		} else {
			/*
			 * Wait a bit here because some EP devices
			 * take longer to come up.
			 */
			msleep(1000);
		}

		/* Check for PCIe link-up status again. */
		port_status.word =
			__gxio_mmio_read(trio_context->mmio_base_mac +
					 reg_offset);
		if (!port_status.dl_up) {
			if (pcie_ports[trio_index].ports[mac].removable) {
				pr_info("PCI: link is down, MAC %d on TRIO %d\n",
					mac, trio_index);
				pr_info("This is expected if no PCIe card is connected to this link\n");
			} else
				pr_err("PCI: link is down, MAC %d on TRIO %d\n",
				       mac, trio_index);
			continue;
		}

		/*
		 * Ensure that the link can come out of L1 power down state.
		 * Strictly speaking, this is needed only in the case of
		 * heavy RC-initiated DMAs.
		 */
		reg_offset =
			(TRIO_PCIE_INTFC_TX_FIFO_CTL <<
				TRIO_CFG_REGION_ADDR__REG_SHIFT) |
			(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE <<
				TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
			(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);
		tx_fifo_ctl.word =
			__gxio_mmio_read(trio_context->mmio_base_mac +
					 reg_offset);
		tx_fifo_ctl.min_p_credits = 0;
		__gxio_mmio_write(trio_context->mmio_base_mac + reg_offset,
				  tx_fifo_ctl.word);

		/*
		 * Change the device ID so that Linux bus crawl doesn't confuse
		 * the internal bridge with any Tilera endpoints.
		 */
		reg_offset =
			(TRIO_PCIE_RC_DEVICE_ID_VEN_ID <<
				TRIO_CFG_REGION_ADDR__REG_SHIFT) |
			(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
				TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
			(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

		__gxio_mmio_write32(trio_context->mmio_base_mac + reg_offset,
				    (TILERA_GX36_RC_DEV_ID <<
				    TRIO_PCIE_RC_DEVICE_ID_VEN_ID__DEV_ID_SHIFT) |
				    TILERA_VENDOR_ID);

		/* Set the internal P2P bridge class code. */
		reg_offset =
			(TRIO_PCIE_RC_REVISION_ID <<
				TRIO_CFG_REGION_ADDR__REG_SHIFT) |
			(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD <<
				TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
			(mac << TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

		class_code_revision =
			__gxio_mmio_read32(trio_context->mmio_base_mac +
					   reg_offset);
		class_code_revision = (class_code_revision & 0xff) |
			(PCI_CLASS_BRIDGE_PCI << 16);

		__gxio_mmio_write32(trio_context->mmio_base_mac +
				    reg_offset, class_code_revision);

#ifdef USE_SHARED_PCIE_CONFIG_REGION

		/* Map in the MMIO space for the PIO region. */
		offset = HV_TRIO_PIO_OFFSET(trio_context->pio_cfg_index) |
			(((unsigned long long)mac) <<
			TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR__MAC_SHIFT);

#else

		/* Alloc a PIO region for PCI config access per MAC. */
		ret = gxio_trio_alloc_pio_regions(trio_context, 1, 0, 0);
		if (ret < 0) {
			pr_err("PCI: PCI CFG PIO alloc failure for mac %d on TRIO %d, give up\n",
			       mac, trio_index);

			continue;
		}

		trio_context->pio_cfg_index[mac] = ret;

		/* For PIO CFG, the bus_address_hi parameter is 0. */
		ret = gxio_trio_init_pio_region_aux(trio_context,
			trio_context->pio_cfg_index[mac],
			mac, 0, HV_TRIO_PIO_FLAG_CONFIG_SPACE);
		if (ret < 0) {
			pr_err("PCI: PCI CFG PIO init failure for mac %d on TRIO %d, give up\n",
			       mac, trio_index);

			continue;
		}

		offset = HV_TRIO_PIO_OFFSET(trio_context->pio_cfg_index[mac]) |
			(((unsigned long long)mac) <<
			TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR__MAC_SHIFT);

#endif

		/*
		 * To save VMALLOC space, we take advantage of the fact that
		 * bit 29 in the PIO CFG address format is reserved 0. With
		 * TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR__MAC_SHIFT being 30,
		 * this cuts VMALLOC space usage from 1GB to 512MB per mac.
		 */
		trio_context->mmio_base_pio_cfg[mac] =
			iorpc_ioremap(trio_context->fd, offset, (1UL <<
			(TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR__MAC_SHIFT - 1)));
		if (trio_context->mmio_base_pio_cfg[mac] == NULL) {
			pr_err("PCI: PIO map failure for mac %d on TRIO %d\n",
			       mac, trio_index);

			continue;
		}

		/* Initialize the PCIe interrupts. */
		if (tile_init_irqs(controller)) {
			pr_err("PCI: IRQs init failure for mac %d on TRIO %d\n",
				mac, trio_index);

			continue;
		}

		/*
		 * The PCI memory resource is located above the PA space.
		 * The memory range for the PCI root bus should not overlap
		 * with the physical RAM.
		 */
		pci_add_resource_offset(&resources, &controller->mem_space,
					controller->mem_offset);
		pci_add_resource(&resources, &controller->io_space);
		controller->first_busno = next_busno;
		bus = pci_scan_root_bus(NULL, next_busno, controller->ops,
					controller, &resources);
		controller->root_bus = bus;
		next_busno = bus->busn_res.end + 1;
	}

	/* Do machine dependent PCI interrupt routing */
	pci_fixup_irqs(pci_common_swizzle, tile_map_irq);

	/*
	 * This comes from the generic Linux PCI driver.
	 *
	 * It allocates all of the resources (I/O memory, etc)
	 * associated with the devices read in above.
	 */
	pci_assign_unassigned_resources();

	/* Record the I/O resources in the PCI controller structure. */
	for (i = 0; i < num_rc_controllers; i++) {
		struct pci_controller *controller = &pci_controllers[i];
		gxio_trio_context_t *trio_context = controller->trio;
		struct pci_bus *root_bus = pci_controllers[i].root_bus;
		int ret;
		int j;

		/*
		 * Skip controllers that are not properly initialized or
		 * have down links.
		 */
		if (root_bus == NULL)
			continue;

		/* Configure the max_payload_size values for this domain. */
		fixup_read_and_payload_sizes(controller);

		/* Alloc a PIO region for PCI memory access for each RC port. */
		ret = gxio_trio_alloc_pio_regions(trio_context, 1, 0, 0);
		if (ret < 0) {
			pr_err("PCI: MEM PIO alloc failure on TRIO %d mac %d, give up\n",
			       controller->trio_index, controller->mac);

			continue;
		}

		controller->pio_mem_index = ret;

		/*
		 * For PIO MEM, the bus_address_hi parameter is hard-coded 0
		 * because we always assign 32-bit PCI bus BAR ranges.
		 */
		ret = gxio_trio_init_pio_region_aux(trio_context,
						    controller->pio_mem_index,
						    controller->mac,
						    0,
						    0);
		if (ret < 0) {
			pr_err("PCI: MEM PIO init failure on TRIO %d mac %d, give up\n",
			       controller->trio_index, controller->mac);

			continue;
		}

#ifdef CONFIG_TILE_PCI_IO
		/*
		 * Alloc a PIO region for PCI I/O space access for each RC port.
		 */
		ret = gxio_trio_alloc_pio_regions(trio_context, 1, 0, 0);
		if (ret < 0) {
			pr_err("PCI: I/O PIO alloc failure on TRIO %d mac %d, give up\n",
			       controller->trio_index, controller->mac);

			continue;
		}

		controller->pio_io_index = ret;

		/*
		 * For PIO IO, the bus_address_hi parameter is hard-coded 0
		 * because PCI I/O address space is 32-bit.
		 */
		ret = gxio_trio_init_pio_region_aux(trio_context,
						    controller->pio_io_index,
						    controller->mac,
						    0,
						    HV_TRIO_PIO_FLAG_IO_SPACE);
		if (ret < 0) {
			pr_err("PCI: I/O PIO init failure on TRIO %d mac %d, give up\n",
			       controller->trio_index, controller->mac);

			continue;
		}
#endif

		/*
		 * Configure a Mem-Map region for each memory controller so
		 * that Linux can map all of its PA space to the PCI bus.
		 * Use the IOMMU to handle hash-for-home memory.
		 */
		for_each_online_node(j) {
			unsigned long start_pfn = node_start_pfn[j];
			unsigned long end_pfn = node_end_pfn[j];
			unsigned long nr_pages = end_pfn - start_pfn;

			ret = gxio_trio_alloc_memory_maps(trio_context, 1, 0,
							  0);
			if (ret < 0) {
				pr_err("PCI: Mem-Map alloc failure on TRIO %d mac %d for MC %d, give up\n",
				       controller->trio_index, controller->mac,
				       j);

				goto alloc_mem_map_failed;
			}

			controller->mem_maps[j] = ret;

			/*
			 * Initialize the Mem-Map and the I/O MMU so that all
			 * the physical memory can be accessed by the endpoint
			 * devices. The base bus address is set to the base CPA
			 * of this memory controller plus an offset (see pci.h).
			 * The region's base VA is set to the base CPA. The
			 * I/O MMU table essentially translates the CPA to
			 * the real PA. Implicitly, for node 0, we create
			 * a separate Mem-Map region that serves as the inbound
			 * window for legacy 32-bit devices. This is a direct
			 * map of the low 4GB CPA space.
			 */
			ret = gxio_trio_init_memory_map_mmu_aux(trio_context,
				controller->mem_maps[j],
				start_pfn << PAGE_SHIFT,
				nr_pages << PAGE_SHIFT,
				trio_context->asid,
				controller->mac,
				(start_pfn << PAGE_SHIFT) +
				TILE_PCI_MEM_MAP_BASE_OFFSET,
				j,
				GXIO_TRIO_ORDER_MODE_UNORDERED);
			if (ret < 0) {
				pr_err("PCI: Mem-Map init failure on TRIO %d mac %d for MC %d, give up\n",
				       controller->trio_index, controller->mac,
				       j);

				goto alloc_mem_map_failed;
			}
			continue;

alloc_mem_map_failed:
			break;
		}

		pci_bus_add_devices(root_bus);
	}

	return 0;
}
subsys_initcall(pcibios_init);

/* No bus fixups needed. */
void pcibios_fixup_bus(struct pci_bus *bus)
{
}

/* Process any "pci=" kernel boot arguments. */
char *__init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	}
	return str;
}

/*
 * Called for each device after PCI setup is done.
 * We initialize the PCI device capabilities conservatively, assuming that
 * all devices can only address the 32-bit DMA space. The exception here is
 * that the device dma_offset is set to the value that matches the 64-bit
 * capable devices. This is OK because dma_offset is not used by legacy
 * dma_ops, nor by the hybrid dma_ops's streaming DMAs, which are 64-bit ops.
 * This implementation matches the kernel design of setting PCI devices'
 * coherent_dma_mask to 0xffffffffull by default, allowing the device drivers
 * to skip calling pci_set_consistent_dma_mask(DMA_BIT_MASK(32)).
 */
static void pcibios_fixup_final(struct pci_dev *pdev)
{
	set_dma_ops(&pdev->dev, gx_legacy_pci_dma_map_ops);
	set_dma_offset(&pdev->dev, TILE_PCI_MEM_MAP_BASE_OFFSET);
	pdev->dev.archdata.max_direct_dma_addr =
		TILE_PCI_MAX_DIRECT_DMA_ADDRESS;
	pdev->dev.coherent_dma_mask = TILE_PCI_MAX_DIRECT_DMA_ADDRESS;
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, pcibios_fixup_final);

/* Map a PCI MMIO bus address into VA space. */
void __iomem *ioremap(resource_size_t phys_addr, unsigned long size)
{
	struct pci_controller *controller = NULL;
	resource_size_t bar_start;
	resource_size_t bar_end;
	resource_size_t offset;
	resource_size_t start;
	resource_size_t end;
	int trio_fd;
	int i;

	start = phys_addr;
	end = phys_addr + size - 1;

	/*
	 * By searching phys_addr in each controller's mem_space, we can
	 * determine the controller that should accept the PCI memory access.
	 */
	for (i = 0; i < num_rc_controllers; i++) {
		/*
		 * Skip controllers that are not properly initialized or
		 * have down links.
		 */
		if (pci_controllers[i].root_bus == NULL)
			continue;

		bar_start = pci_controllers[i].mem_space.start;
		bar_end = pci_controllers[i].mem_space.end;

		if ((start >= bar_start) && (end <= bar_end)) {
			controller = &pci_controllers[i];
			break;
		}
	}

	if (controller == NULL)
		return NULL;

	trio_fd = controller->trio->fd;

	/* Convert the resource start to the bus address offset. */
	start = phys_addr - controller->mem_offset;

	offset = HV_TRIO_PIO_OFFSET(controller->pio_mem_index) + start;

	/* We need to keep the PCI bus address's in-page offset in the VA. */
	return iorpc_ioremap(trio_fd, offset, size) +
		(start & (PAGE_SIZE - 1));
}
EXPORT_SYMBOL(ioremap);

#ifdef CONFIG_TILE_PCI_IO
/* Map a PCI I/O address into VA space. */
void __iomem *ioport_map(unsigned long port, unsigned int size)
{
	struct pci_controller *controller = NULL;
	resource_size_t bar_start;
	resource_size_t bar_end;
	resource_size_t offset;
	resource_size_t start;
	resource_size_t end;
	int trio_fd;
	int i;

	start = port;
	end = port + size - 1;

	/*
	 * By searching the port in each controller's io_space, we can
	 * determine the controller that should accept the PCI I/O access.
	 */
	for (i = 0; i < num_rc_controllers; i++) {
		/*
		 * Skip controllers that are not properly initialized or
		 * have down links.
		 */
		if (pci_controllers[i].root_bus == NULL)
			continue;

		bar_start = pci_controllers[i].io_space.start;
		bar_end = pci_controllers[i].io_space.end;

		if ((start >= bar_start) && (end <= bar_end)) {
			controller = &pci_controllers[i];
			break;
		}
	}

	if (controller == NULL)
		return NULL;

	trio_fd = controller->trio->fd;

	/* Convert the resource start to the bus address offset. */
	port -= controller->io_space.start;

	offset = HV_TRIO_PIO_OFFSET(controller->pio_io_index) + port;

	/* We need to keep the PCI bus address's in-page offset in the VA. */
	return iorpc_ioremap(trio_fd, offset, size) + (port & (PAGE_SIZE - 1));
}
EXPORT_SYMBOL(ioport_map);

void ioport_unmap(void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL(ioport_unmap);
#endif

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL(pci_iounmap);

/****************************************************************
 *
 * Tile PCI config space read/write routines
 *
 ****************************************************************/

/*
 * These are the normal read and write ops
 * These are expanded with macros from  pci_bus_read_config_byte() etc.
 *
 * devfn is the combined PCI device & function.
 *
 * offset is in bytes, from the start of config space for the
 * specified bus & device.
 */
static int tile_cfg_read(struct pci_bus *bus, unsigned int devfn, int offset,
			 int size, u32 *val)
{
	struct pci_controller *controller = bus->sysdata;
	gxio_trio_context_t *trio_context = controller->trio;
	int busnum = bus->number & 0xff;
	int device = PCI_SLOT(devfn);
	int function = PCI_FUNC(devfn);
	int config_type = 1;
	TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR_t cfg_addr;
	void *mmio_addr;

	/*
	 * Map all accesses to the local device on root bus into the
	 * MMIO space of the MAC. Accesses to the downstream devices
	 * go to the PIO space.
	 */
	if (pci_is_root_bus(bus)) {
		if (device == 0) {
			/*
			 * This is the internal downstream P2P bridge,
			 * access directly.
			 */
			unsigned int reg_offset;

			reg_offset = ((offset & 0xFFF) <<
				TRIO_CFG_REGION_ADDR__REG_SHIFT) |
				(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_PROTECTED
				<< TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
				(controller->mac <<
					TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

			mmio_addr = trio_context->mmio_base_mac + reg_offset;

			goto valid_device;

		} else {
			/*
			 * We fake an empty device for (device > 0),
			 * since there is only one device on bus 0.
			 */
			goto invalid_device;
		}
	}

	/*
	 * Accesses to the directly attached device have to be
	 * sent as type-0 configs.
	 */
	if (busnum == (controller->first_busno + 1)) {
		/*
		 * There is only one device off of our built-in P2P bridge.
		 */
		if (device != 0)
			goto invalid_device;

		config_type = 0;
	}

	cfg_addr.word = 0;
	cfg_addr.reg_addr = (offset & 0xFFF);
	cfg_addr.fn = function;
	cfg_addr.dev = device;
	cfg_addr.bus = busnum;
	cfg_addr.type = config_type;

	/*
	 * Note that we don't set the mac field in cfg_addr because the
	 * mapping is per port.
	 */
	mmio_addr = trio_context->mmio_base_pio_cfg[controller->mac] +
		cfg_addr.word;

valid_device:

	switch (size) {
	case 4:
		*val = __gxio_mmio_read32(mmio_addr);
		break;

	case 2:
		*val = __gxio_mmio_read16(mmio_addr);
		break;

	case 1:
		*val = __gxio_mmio_read8(mmio_addr);
		break;

	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	TRACE_CFG_RD(size, *val, busnum, device, function, offset);

	return 0;

invalid_device:

	switch (size) {
	case 4:
		*val = 0xFFFFFFFF;
		break;

	case 2:
		*val = 0xFFFF;
		break;

	case 1:
		*val = 0xFF;
		break;

	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return 0;
}


/*
 * See tile_cfg_read() for relevant comments.
 * Note that "val" is the value to write, not a pointer to that value.
 */
static int tile_cfg_write(struct pci_bus *bus, unsigned int devfn, int offset,
			  int size, u32 val)
{
	struct pci_controller *controller = bus->sysdata;
	gxio_trio_context_t *trio_context = controller->trio;
	int busnum = bus->number & 0xff;
	int device = PCI_SLOT(devfn);
	int function = PCI_FUNC(devfn);
	int config_type = 1;
	TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR_t cfg_addr;
	void *mmio_addr;
	u32 val_32 = (u32)val;
	u16 val_16 = (u16)val;
	u8 val_8 = (u8)val;

	/*
	 * Map all accesses to the local device on root bus into the
	 * MMIO space of the MAC. Accesses to the downstream devices
	 * go to the PIO space.
	 */
	if (pci_is_root_bus(bus)) {
		if (device == 0) {
			/*
			 * This is the internal downstream P2P bridge,
			 * access directly.
			 */
			unsigned int reg_offset;

			reg_offset = ((offset & 0xFFF) <<
				TRIO_CFG_REGION_ADDR__REG_SHIFT) |
				(TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_PROTECTED
				<< TRIO_CFG_REGION_ADDR__INTFC_SHIFT ) |
				(controller->mac <<
					TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT);

			mmio_addr = trio_context->mmio_base_mac + reg_offset;

			goto valid_device;

		} else {
			/*
			 * We fake an empty device for (device > 0),
			 * since there is only one device on bus 0.
			 */
			goto invalid_device;
		}
	}

	/*
	 * Accesses to the directly attached device have to be
	 * sent as type-0 configs.
	 */
	if (busnum == (controller->first_busno + 1)) {
		/*
		 * There is only one device off of our built-in P2P bridge.
		 */
		if (device != 0)
			goto invalid_device;

		config_type = 0;
	}

	cfg_addr.word = 0;
	cfg_addr.reg_addr = (offset & 0xFFF);
	cfg_addr.fn = function;
	cfg_addr.dev = device;
	cfg_addr.bus = busnum;
	cfg_addr.type = config_type;

	/*
	 * Note that we don't set the mac field in cfg_addr because the
	 * mapping is per port.
	 */
	mmio_addr = trio_context->mmio_base_pio_cfg[controller->mac] +
			cfg_addr.word;

valid_device:

	switch (size) {
	case 4:
		__gxio_mmio_write32(mmio_addr, val_32);
		TRACE_CFG_WR(size, val_32, busnum, device, function, offset);
		break;

	case 2:
		__gxio_mmio_write16(mmio_addr, val_16);
		TRACE_CFG_WR(size, val_16, busnum, device, function, offset);
		break;

	case 1:
		__gxio_mmio_write8(mmio_addr, val_8);
		TRACE_CFG_WR(size, val_8, busnum, device, function, offset);
		break;

	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

invalid_device:

	return 0;
}


static struct pci_ops tile_cfg_ops = {
	.read =         tile_cfg_read,
	.write =        tile_cfg_write,
};


/* MSI support starts here. */
static unsigned int tilegx_msi_startup(struct irq_data *d)
{
	if (irq_data_get_msi_desc(d))
		pci_msi_unmask_irq(d);

	return 0;
}

static void tilegx_msi_ack(struct irq_data *d)
{
	__insn_mtspr(SPR_IPI_EVENT_RESET_K, 1UL << d->irq);
}

static void tilegx_msi_mask(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	__insn_mtspr(SPR_IPI_MASK_SET_K, 1UL << d->irq);
}

static void tilegx_msi_unmask(struct irq_data *d)
{
	__insn_mtspr(SPR_IPI_MASK_RESET_K, 1UL << d->irq);
	pci_msi_unmask_irq(d);
}

static struct irq_chip tilegx_msi_chip = {
	.name			= "tilegx_msi",
	.irq_startup		= tilegx_msi_startup,
	.irq_ack		= tilegx_msi_ack,
	.irq_mask		= tilegx_msi_mask,
	.irq_unmask		= tilegx_msi_unmask,

	/* TBD: support set_affinity. */
};

int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	struct pci_controller *controller;
	gxio_trio_context_t *trio_context;
	struct msi_msg msg;
	int default_irq;
	uint64_t mem_map_base;
	uint64_t mem_map_limit;
	u64 msi_addr;
	int mem_map;
	int cpu;
	int irq;
	int ret;

	irq = irq_alloc_hwirq(-1);
	if (!irq)
		return -ENOSPC;

	/*
	 * Since we use a 64-bit Mem-Map to accept the MSI write, we fail
	 * devices that are not capable of generating a 64-bit message address.
	 * These devices will fall back to using the legacy interrupts.
	 * Most PCIe endpoint devices do support 64-bit message addressing.
	 */
	if (desc->msi_attrib.is_64 == 0) {
		dev_info(&pdev->dev, "64-bit MSI message address not supported, falling back to legacy interrupts\n");

		ret = -ENOMEM;
		goto is_64_failure;
	}

	default_irq = desc->msi_attrib.default_irq;
	controller = irq_get_handler_data(default_irq);

	BUG_ON(!controller);

	trio_context = controller->trio;

	/*
	 * Allocate a scatter-queue that will accept the MSI write and
	 * trigger the TILE-side interrupts. We use the scatter-queue regions
	 * before the mem map regions, because the latter are needed by more
	 * applications.
	 */
	mem_map = gxio_trio_alloc_scatter_queues(trio_context, 1, 0, 0);
	if (mem_map >= 0) {
		TRIO_MAP_SQ_DOORBELL_FMT_t doorbell_template = {{
			.pop = 0,
			.doorbell = 1,
		}};

		mem_map += TRIO_NUM_MAP_MEM_REGIONS;
		mem_map_base = MEM_MAP_INTR_REGIONS_BASE +
			mem_map * MEM_MAP_INTR_REGION_SIZE;
		mem_map_limit = mem_map_base + MEM_MAP_INTR_REGION_SIZE - 1;

		msi_addr = mem_map_base + MEM_MAP_INTR_REGION_SIZE - 8;
		msg.data = (unsigned int)doorbell_template.word;
	} else {
		/* SQ regions are out, allocate from map mem regions. */
		mem_map = gxio_trio_alloc_memory_maps(trio_context, 1, 0, 0);
		if (mem_map < 0) {
			dev_info(&pdev->dev, "%s Mem-Map alloc failure - failed to initialize MSI interrupts - falling back to legacy interrupts\n",
				 desc->msi_attrib.is_msix ? "MSI-X" : "MSI");
			ret = -ENOMEM;
			goto msi_mem_map_alloc_failure;
		}

		mem_map_base = MEM_MAP_INTR_REGIONS_BASE +
			mem_map * MEM_MAP_INTR_REGION_SIZE;
		mem_map_limit = mem_map_base + MEM_MAP_INTR_REGION_SIZE - 1;

		msi_addr = mem_map_base + TRIO_MAP_MEM_REG_INT3 -
			TRIO_MAP_MEM_REG_INT0;

		msg.data = mem_map;
	}

	/* We try to distribute different IRQs to different tiles. */
	cpu = tile_irq_cpu(irq);

	/*
	 * Now call up to the HV to configure the MSI interrupt and
	 * set up the IPI binding.
	 */
	ret = gxio_trio_config_msi_intr(trio_context, cpu_x(cpu), cpu_y(cpu),
					KERNEL_PL, irq, controller->mac,
					mem_map, mem_map_base, mem_map_limit,
					trio_context->asid);
	if (ret < 0) {
		dev_info(&pdev->dev, "HV MSI config failed\n");

		goto hv_msi_config_failure;
	}

	irq_set_msi_desc(irq, desc);

	msg.address_hi = msi_addr >> 32;
	msg.address_lo = msi_addr & 0xffffffff;

	pci_write_msi_msg(irq, &msg);
	irq_set_chip_and_handler(irq, &tilegx_msi_chip, handle_level_irq);
	irq_set_handler_data(irq, controller);

	return 0;

hv_msi_config_failure:
	/* Free mem-map */
msi_mem_map_alloc_failure:
is_64_failure:
	irq_free_hwirq(irq);
	return ret;
}

void arch_teardown_msi_irq(unsigned int irq)
{
	irq_free_hwirq(irq);
}

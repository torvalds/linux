/*
 * drivers/misc/spear13xx_pcie_gadget.c
 *
 * Copyright (C) 2010 ST Microelectronics
 * Pratyush Anand<pratyush.anand@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pci_regs.h>
#include <linux/configfs.h>
#include <mach/pcie.h>
#include <mach/misc_regs.h>

#define IN0_MEM_SIZE	(200 * 1024 * 1024 - 1)
/* In current implementation address translation is done using IN0 only.
 * So IN1 start address and IN0 end address has been kept same
*/
#define IN1_MEM_SIZE	(0 * 1024 * 1024 - 1)
#define IN_IO_SIZE	(20 * 1024 * 1024 - 1)
#define IN_CFG0_SIZE	(12 * 1024 * 1024 - 1)
#define IN_CFG1_SIZE	(12 * 1024 * 1024 - 1)
#define IN_MSG_SIZE	(12 * 1024 * 1024 - 1)
/* Keep default BAR size as 4K*/
/* AORAM would be mapped by default*/
#define INBOUND_ADDR_MASK	(SPEAR13XX_SYSRAM1_SIZE - 1)

#define INT_TYPE_NO_INT	0
#define INT_TYPE_INTX	1
#define INT_TYPE_MSI	2
struct spear_pcie_gadget_config {
	void __iomem *base;
	void __iomem *va_app_base;
	void __iomem *va_dbi_base;
	char int_type[10];
	ulong requested_msi;
	ulong configured_msi;
	ulong bar0_size;
	ulong bar0_rw_offset;
	void __iomem *va_bar0_address;
};

struct pcie_gadget_target {
	struct configfs_subsystem subsys;
	struct spear_pcie_gadget_config config;
};

struct pcie_gadget_target_attr {
	struct configfs_attribute	attr;
	ssize_t		(*show)(struct spear_pcie_gadget_config *config,
						char *buf);
	ssize_t		(*store)(struct spear_pcie_gadget_config *config,
						 const char *buf,
						 size_t count);
};

static void enable_dbi_access(struct pcie_app_reg __iomem *app_reg)
{
	/* Enable DBI access */
	writel(readl(&app_reg->slv_armisc) | (1 << AXI_OP_DBI_ACCESS_ID),
			&app_reg->slv_armisc);
	writel(readl(&app_reg->slv_awmisc) | (1 << AXI_OP_DBI_ACCESS_ID),
			&app_reg->slv_awmisc);

}

static void disable_dbi_access(struct pcie_app_reg __iomem *app_reg)
{
	/* disable DBI access */
	writel(readl(&app_reg->slv_armisc) & ~(1 << AXI_OP_DBI_ACCESS_ID),
			&app_reg->slv_armisc);
	writel(readl(&app_reg->slv_awmisc) & ~(1 << AXI_OP_DBI_ACCESS_ID),
			&app_reg->slv_awmisc);

}

static void spear_dbi_read_reg(struct spear_pcie_gadget_config *config,
		int where, int size, u32 *val)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;
	ulong va_address;

	/* Enable DBI access */
	enable_dbi_access(app_reg);

	va_address = (ulong)config->va_dbi_base + (where & ~0x3);

	*val = readl(va_address);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	/* Disable DBI access */
	disable_dbi_access(app_reg);
}

static void spear_dbi_write_reg(struct spear_pcie_gadget_config *config,
		int where, int size, u32 val)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;
	ulong va_address;

	/* Enable DBI access */
	enable_dbi_access(app_reg);

	va_address = (ulong)config->va_dbi_base + (where & ~0x3);

	if (size == 4)
		writel(val, va_address);
	else if (size == 2)
		writew(val, va_address + (where & 2));
	else if (size == 1)
		writeb(val, va_address + (where & 3));

	/* Disable DBI access */
	disable_dbi_access(app_reg);
}

#define PCI_FIND_CAP_TTL	48

static int pci_find_own_next_cap_ttl(struct spear_pcie_gadget_config *config,
		u32 pos, int cap, int *ttl)
{
	u32 id;

	while ((*ttl)--) {
		spear_dbi_read_reg(config, pos, 1, &pos);
		if (pos < 0x40)
			break;
		pos &= ~3;
		spear_dbi_read_reg(config, pos + PCI_CAP_LIST_ID, 1, &id);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos += PCI_CAP_LIST_NEXT;
	}
	return 0;
}

static int pci_find_own_next_cap(struct spear_pcie_gadget_config *config,
			u32 pos, int cap)
{
	int ttl = PCI_FIND_CAP_TTL;

	return pci_find_own_next_cap_ttl(config, pos, cap, &ttl);
}

static int pci_find_own_cap_start(struct spear_pcie_gadget_config *config,
				u8 hdr_type)
{
	u32 status;

	spear_dbi_read_reg(config, PCI_STATUS, 2, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;

	switch (hdr_type) {
	case PCI_HEADER_TYPE_NORMAL:
	case PCI_HEADER_TYPE_BRIDGE:
		return PCI_CAPABILITY_LIST;
	case PCI_HEADER_TYPE_CARDBUS:
		return PCI_CB_CAPABILITY_LIST;
	default:
		return 0;
	}

	return 0;
}

/*
 * Tell if a device supports a given PCI capability.
 * Returns the address of the requested capability structure within the
 * device's PCI configuration space or 0 in case the device does not
 * support it. Possible values for @cap:
 *
 * %PCI_CAP_ID_PM	Power Management
 * %PCI_CAP_ID_AGP	Accelerated Graphics Port
 * %PCI_CAP_ID_VPD	Vital Product Data
 * %PCI_CAP_ID_SLOTID	Slot Identification
 * %PCI_CAP_ID_MSI	Message Signalled Interrupts
 * %PCI_CAP_ID_CHSWP	CompactPCI HotSwap
 * %PCI_CAP_ID_PCIX	PCI-X
 * %PCI_CAP_ID_EXP	PCI Express
 */
static int pci_find_own_capability(struct spear_pcie_gadget_config *config,
		int cap)
{
	u32 pos;
	u32 hdr_type;

	spear_dbi_read_reg(config, PCI_HEADER_TYPE, 1, &hdr_type);

	pos = pci_find_own_cap_start(config, hdr_type);
	if (pos)
		pos = pci_find_own_next_cap(config, pos, cap);

	return pos;
}

static irqreturn_t spear_pcie_gadget_irq(int irq, void *dev_id)
{
	return 0;
}

/*
 * configfs interfaces show/store functions
 */
static ssize_t pcie_gadget_show_link(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;

	if (readl(&app_reg->app_status_1) & ((u32)1 << XMLH_LINK_UP_ID))
		return sprintf(buf, "UP");
	else
		return sprintf(buf, "DOWN");
}

static ssize_t pcie_gadget_store_link(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;

	if (sysfs_streq(buf, "UP"))
		writel(readl(&app_reg->app_ctrl_0) | (1 << APP_LTSSM_ENABLE_ID),
			&app_reg->app_ctrl_0);
	else if (sysfs_streq(buf, "DOWN"))
		writel(readl(&app_reg->app_ctrl_0)
				& ~(1 << APP_LTSSM_ENABLE_ID),
				&app_reg->app_ctrl_0);
	else
		return -EINVAL;
	return count;
}

static ssize_t pcie_gadget_show_int_type(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	return sprintf(buf, "%s", config->int_type);
}

static ssize_t pcie_gadget_store_int_type(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	u32 cap, vec, flags;
	ulong vector;

	if (sysfs_streq(buf, "INTA"))
		spear_dbi_write_reg(config, PCI_INTERRUPT_LINE, 1, 1);

	else if (sysfs_streq(buf, "MSI")) {
		vector = config->requested_msi;
		vec = 0;
		while (vector > 1) {
			vector /= 2;
			vec++;
		}
		spear_dbi_write_reg(config, PCI_INTERRUPT_LINE, 1, 0);
		cap = pci_find_own_capability(config, PCI_CAP_ID_MSI);
		spear_dbi_read_reg(config, cap + PCI_MSI_FLAGS, 1, &flags);
		flags &= ~PCI_MSI_FLAGS_QMASK;
		flags |= vec << 1;
		spear_dbi_write_reg(config, cap + PCI_MSI_FLAGS, 1, flags);
	} else
		return -EINVAL;

	strcpy(config->int_type, buf);

	return count;
}

static ssize_t pcie_gadget_show_no_of_msi(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;
	u32 cap, vec, flags;
	ulong vector;

	if ((readl(&app_reg->msg_status) & (1 << CFG_MSI_EN_ID))
			!= (1 << CFG_MSI_EN_ID))
		vector = 0;
	else {
		cap = pci_find_own_capability(config, PCI_CAP_ID_MSI);
		spear_dbi_read_reg(config, cap + PCI_MSI_FLAGS, 1, &flags);
		flags &= ~PCI_MSI_FLAGS_QSIZE;
		vec = flags >> 4;
		vector = 1;
		while (vec--)
			vector *= 2;
	}
	config->configured_msi = vector;

	return sprintf(buf, "%lu", vector);
}

static ssize_t pcie_gadget_store_no_of_msi(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	if (strict_strtoul(buf, 0, &config->requested_msi))
		return -EINVAL;
	if (config->requested_msi > 32)
		config->requested_msi = 32;

	return count;
}

static ssize_t pcie_gadget_store_inta(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;
	ulong en;

	if (strict_strtoul(buf, 0, &en))
		return -EINVAL;

	if (en)
		writel(readl(&app_reg->app_ctrl_0) | (1 << SYS_INT_ID),
				&app_reg->app_ctrl_0);
	else
		writel(readl(&app_reg->app_ctrl_0) & ~(1 << SYS_INT_ID),
				&app_reg->app_ctrl_0);

	return count;
}

static ssize_t pcie_gadget_store_send_msi(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;
	ulong vector;
	u32 ven_msi;

	if (strict_strtoul(buf, 0, &vector))
		return -EINVAL;

	if (!config->configured_msi)
		return -EINVAL;

	if (vector >= config->configured_msi)
		return -EINVAL;

	ven_msi = readl(&app_reg->ven_msi_1);
	ven_msi &= ~VEN_MSI_FUN_NUM_MASK;
	ven_msi |= 0 << VEN_MSI_FUN_NUM_ID;
	ven_msi &= ~VEN_MSI_TC_MASK;
	ven_msi |= 0 << VEN_MSI_TC_ID;
	ven_msi &= ~VEN_MSI_VECTOR_MASK;
	ven_msi |= vector << VEN_MSI_VECTOR_ID;

	/* generating interrupt for msi vector */
	ven_msi |= VEN_MSI_REQ_EN;
	writel(ven_msi, &app_reg->ven_msi_1);
	udelay(1);
	ven_msi &= ~VEN_MSI_REQ_EN;
	writel(ven_msi, &app_reg->ven_msi_1);

	return count;
}

static ssize_t pcie_gadget_show_vendor_id(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	u32 id;

	spear_dbi_read_reg(config, PCI_VENDOR_ID, 2, &id);

	return sprintf(buf, "%x", id);
}

static ssize_t pcie_gadget_store_vendor_id(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	ulong id;

	if (strict_strtoul(buf, 0, &id))
		return -EINVAL;

	spear_dbi_write_reg(config, PCI_VENDOR_ID, 2, id);

	return count;
}

static ssize_t pcie_gadget_show_device_id(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	u32 id;

	spear_dbi_read_reg(config, PCI_DEVICE_ID, 2, &id);

	return sprintf(buf, "%x", id);
}

static ssize_t pcie_gadget_store_device_id(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	ulong id;

	if (strict_strtoul(buf, 0, &id))
		return -EINVAL;

	spear_dbi_write_reg(config, PCI_DEVICE_ID, 2, id);

	return count;
}

static ssize_t pcie_gadget_show_bar0_size(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	return sprintf(buf, "%lx", config->bar0_size);
}

static ssize_t pcie_gadget_store_bar0_size(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	ulong size;
	u32 pos, pos1;
	u32 no_of_bit = 0;

	if (strict_strtoul(buf, 0, &size))
		return -EINVAL;
	/* min bar size is 256 */
	if (size <= 0x100)
		size = 0x100;
	/* max bar size is 1MB*/
	else if (size >= 0x100000)
		size = 0x100000;
	else {
		pos = 0;
		pos1 = 0;
		while (pos < 21) {
			pos = find_next_bit((ulong *)&size, 21, pos);
			if (pos != 21)
				pos1 = pos + 1;
			pos++;
			no_of_bit++;
		}
		if (no_of_bit == 2)
			pos1--;

		size = 1 << pos1;
	}
	config->bar0_size = size;
	spear_dbi_write_reg(config, PCIE_BAR0_MASK_REG, 4, size - 1);

	return count;
}

static ssize_t pcie_gadget_show_bar0_address(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;

	u32 address = readl(&app_reg->pim0_mem_addr_start);

	return sprintf(buf, "%x", address);
}

static ssize_t pcie_gadget_store_bar0_address(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;
	ulong address;

	if (strict_strtoul(buf, 0, &address))
		return -EINVAL;

	address &= ~(config->bar0_size - 1);
	if (config->va_bar0_address)
		iounmap(config->va_bar0_address);
	config->va_bar0_address = ioremap(address, config->bar0_size);
	if (!config->va_bar0_address)
		return -ENOMEM;

	writel(address, &app_reg->pim0_mem_addr_start);

	return count;
}

static ssize_t pcie_gadget_show_bar0_rw_offset(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	return sprintf(buf, "%lx", config->bar0_rw_offset);
}

static ssize_t pcie_gadget_store_bar0_rw_offset(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	ulong offset;

	if (strict_strtoul(buf, 0, &offset))
		return -EINVAL;

	if (offset % 4)
		return -EINVAL;

	config->bar0_rw_offset = offset;

	return count;
}

static ssize_t pcie_gadget_show_bar0_data(
		struct spear_pcie_gadget_config *config,
		char *buf)
{
	ulong data;

	if (!config->va_bar0_address)
		return -ENOMEM;

	data = readl((ulong)config->va_bar0_address + config->bar0_rw_offset);

	return sprintf(buf, "%lx", data);
}

static ssize_t pcie_gadget_store_bar0_data(
		struct spear_pcie_gadget_config *config,
		const char *buf, size_t count)
{
	ulong data;

	if (strict_strtoul(buf, 0, &data))
		return -EINVAL;

	if (!config->va_bar0_address)
		return -ENOMEM;

	writel(data, (ulong)config->va_bar0_address + config->bar0_rw_offset);

	return count;
}

/*
 * Attribute definitions.
 */

#define PCIE_GADGET_TARGET_ATTR_RO(_name)				\
static struct pcie_gadget_target_attr pcie_gadget_target_##_name =	\
	__CONFIGFS_ATTR(_name, S_IRUGO, pcie_gadget_show_##_name, NULL)

#define PCIE_GADGET_TARGET_ATTR_WO(_name)				\
static struct pcie_gadget_target_attr pcie_gadget_target_##_name =	\
	__CONFIGFS_ATTR(_name, S_IWUSR, NULL, pcie_gadget_store_##_name)

#define PCIE_GADGET_TARGET_ATTR_RW(_name)				\
static struct pcie_gadget_target_attr pcie_gadget_target_##_name =	\
	__CONFIGFS_ATTR(_name, S_IRUGO | S_IWUSR, pcie_gadget_show_##_name, \
			pcie_gadget_store_##_name)
PCIE_GADGET_TARGET_ATTR_RW(link);
PCIE_GADGET_TARGET_ATTR_RW(int_type);
PCIE_GADGET_TARGET_ATTR_RW(no_of_msi);
PCIE_GADGET_TARGET_ATTR_WO(inta);
PCIE_GADGET_TARGET_ATTR_WO(send_msi);
PCIE_GADGET_TARGET_ATTR_RW(vendor_id);
PCIE_GADGET_TARGET_ATTR_RW(device_id);
PCIE_GADGET_TARGET_ATTR_RW(bar0_size);
PCIE_GADGET_TARGET_ATTR_RW(bar0_address);
PCIE_GADGET_TARGET_ATTR_RW(bar0_rw_offset);
PCIE_GADGET_TARGET_ATTR_RW(bar0_data);

static struct configfs_attribute *pcie_gadget_target_attrs[] = {
	&pcie_gadget_target_link.attr,
	&pcie_gadget_target_int_type.attr,
	&pcie_gadget_target_no_of_msi.attr,
	&pcie_gadget_target_inta.attr,
	&pcie_gadget_target_send_msi.attr,
	&pcie_gadget_target_vendor_id.attr,
	&pcie_gadget_target_device_id.attr,
	&pcie_gadget_target_bar0_size.attr,
	&pcie_gadget_target_bar0_address.attr,
	&pcie_gadget_target_bar0_rw_offset.attr,
	&pcie_gadget_target_bar0_data.attr,
	NULL,
};

static struct pcie_gadget_target *to_target(struct config_item *item)
{
	return item ?
		container_of(to_configfs_subsystem(to_config_group(item)),
				struct pcie_gadget_target, subsys) : NULL;
}

/*
 * Item operations and type for pcie_gadget_target.
 */

static ssize_t pcie_gadget_target_attr_show(struct config_item *item,
					   struct configfs_attribute *attr,
					   char *buf)
{
	ssize_t ret = -EINVAL;
	struct pcie_gadget_target *target = to_target(item);
	struct pcie_gadget_target_attr *t_attr =
		container_of(attr, struct pcie_gadget_target_attr, attr);

	if (t_attr->show)
		ret = t_attr->show(&target->config, buf);
	return ret;
}

static ssize_t pcie_gadget_target_attr_store(struct config_item *item,
					struct configfs_attribute *attr,
					const char *buf,
					size_t count)
{
	ssize_t ret = -EINVAL;
	struct pcie_gadget_target *target = to_target(item);
	struct pcie_gadget_target_attr *t_attr =
		container_of(attr, struct pcie_gadget_target_attr, attr);

	if (t_attr->store)
		ret = t_attr->store(&target->config, buf, count);
	return ret;
}

static struct configfs_item_operations pcie_gadget_target_item_ops = {
	.show_attribute		= pcie_gadget_target_attr_show,
	.store_attribute	= pcie_gadget_target_attr_store,
};

static struct config_item_type pcie_gadget_target_type = {
	.ct_attrs		= pcie_gadget_target_attrs,
	.ct_item_ops		= &pcie_gadget_target_item_ops,
	.ct_owner		= THIS_MODULE,
};

static void spear13xx_pcie_device_init(struct spear_pcie_gadget_config *config)
{
	struct pcie_app_reg __iomem *app_reg = config->va_app_base;

	/*setup registers for outbound translation */

	writel(config->base, &app_reg->in0_mem_addr_start);
	writel(app_reg->in0_mem_addr_start + IN0_MEM_SIZE,
			&app_reg->in0_mem_addr_limit);
	writel(app_reg->in0_mem_addr_limit + 1, &app_reg->in1_mem_addr_start);
	writel(app_reg->in1_mem_addr_start + IN1_MEM_SIZE,
			&app_reg->in1_mem_addr_limit);
	writel(app_reg->in1_mem_addr_limit + 1, &app_reg->in_io_addr_start);
	writel(app_reg->in_io_addr_start + IN_IO_SIZE,
			&app_reg->in_io_addr_limit);
	writel(app_reg->in_io_addr_limit + 1, &app_reg->in_cfg0_addr_start);
	writel(app_reg->in_cfg0_addr_start + IN_CFG0_SIZE,
			&app_reg->in_cfg0_addr_limit);
	writel(app_reg->in_cfg0_addr_limit + 1, &app_reg->in_cfg1_addr_start);
	writel(app_reg->in_cfg1_addr_start + IN_CFG1_SIZE,
			&app_reg->in_cfg1_addr_limit);
	writel(app_reg->in_cfg1_addr_limit + 1, &app_reg->in_msg_addr_start);
	writel(app_reg->in_msg_addr_start + IN_MSG_SIZE,
			&app_reg->in_msg_addr_limit);

	writel(app_reg->in0_mem_addr_start, &app_reg->pom0_mem_addr_start);
	writel(app_reg->in1_mem_addr_start, &app_reg->pom1_mem_addr_start);
	writel(app_reg->in_io_addr_start, &app_reg->pom_io_addr_start);

	/*setup registers for inbound translation */

	/* Keep AORAM mapped at BAR0 as default */
	config->bar0_size = INBOUND_ADDR_MASK + 1;
	spear_dbi_write_reg(config, PCIE_BAR0_MASK_REG, 4, INBOUND_ADDR_MASK);
	spear_dbi_write_reg(config, PCI_BASE_ADDRESS_0, 4, 0xC);
	config->va_bar0_address = ioremap(SPEAR13XX_SYSRAM1_BASE,
			config->bar0_size);

	writel(SPEAR13XX_SYSRAM1_BASE, &app_reg->pim0_mem_addr_start);
	writel(0, &app_reg->pim1_mem_addr_start);
	writel(INBOUND_ADDR_MASK + 1, &app_reg->mem0_addr_offset_limit);

	writel(0x0, &app_reg->pim_io_addr_start);
	writel(0x0, &app_reg->pim_io_addr_start);
	writel(0x0, &app_reg->pim_rom_addr_start);

	writel(DEVICE_TYPE_EP | (1 << MISCTRL_EN_ID)
			| ((u32)1 << REG_TRANSLATION_ENABLE),
			&app_reg->app_ctrl_0);
	/* disable all rx interrupts */
	writel(0, &app_reg->int_mask);

	/* Select INTA as default*/
	spear_dbi_write_reg(config, PCI_INTERRUPT_LINE, 1, 1);
}

static int __devinit spear_pcie_gadget_probe(struct platform_device *pdev)
{
	struct resource *res0, *res1;
	unsigned int status = 0;
	int irq;
	struct clk *clk;
	static struct pcie_gadget_target *target;
	struct spear_pcie_gadget_config *config;
	struct config_item		*cg_item;
	struct configfs_subsystem *subsys;

	/* get resource for application registers*/

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res0) {
		dev_err(&pdev->dev, "no resource defined\n");
		return -EBUSY;
	}
	if (!request_mem_region(res0->start, resource_size(res0),
				pdev->name)) {
		dev_err(&pdev->dev, "pcie gadget region already	claimed\n");
		return -EBUSY;
	}
	/* get resource for dbi registers*/

	res1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res1) {
		dev_err(&pdev->dev, "no resource defined\n");
		goto err_rel_res0;
	}
	if (!request_mem_region(res1->start, resource_size(res1),
				pdev->name)) {
		dev_err(&pdev->dev, "pcie gadget region already	claimed\n");
		goto err_rel_res0;
	}

	target = kzalloc(sizeof(*target), GFP_KERNEL);
	if (!target) {
		dev_err(&pdev->dev, "out of memory\n");
		status = -ENOMEM;
		goto err_rel_res;
	}

	cg_item = &target->subsys.su_group.cg_item;
	sprintf(cg_item->ci_namebuf, "pcie_gadget.%d", pdev->id);
	cg_item->ci_type	= &pcie_gadget_target_type;
	config = &target->config;
	config->va_app_base = (void __iomem *)ioremap(res0->start,
			resource_size(res0));
	if (!config->va_app_base) {
		dev_err(&pdev->dev, "ioremap fail\n");
		status = -ENOMEM;
		goto err_kzalloc;
	}

	config->base = (void __iomem *)res1->start;

	config->va_dbi_base = (void __iomem *)ioremap(res1->start,
			resource_size(res1));
	if (!config->va_dbi_base) {
		dev_err(&pdev->dev, "ioremap fail\n");
		status = -ENOMEM;
		goto err_iounmap_app;
	}

	dev_set_drvdata(&pdev->dev, target);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no update irq?\n");
		status = irq;
		goto err_iounmap;
	}

	status = request_irq(irq, spear_pcie_gadget_irq, 0, pdev->name, NULL);
	if (status) {
		dev_err(&pdev->dev,
			"pcie gadget interrupt IRQ%d already claimed\n", irq);
		goto err_iounmap;
	}

	/* Register configfs hooks */
	subsys = &target->subsys;
	config_group_init(&subsys->su_group);
	mutex_init(&subsys->su_mutex);
	status = configfs_register_subsystem(subsys);
	if (status)
		goto err_irq;

	/*
	 * init basic pcie application registers
	 * do not enable clock if it is PCIE0.Ideally , all controller should
	 * have been independent from others with respect to clock. But PCIE1
	 * and 2 depends on PCIE0.So PCIE0 clk is provided during board init.
	 */
	if (pdev->id == 1) {
		/*
		 * Ideally CFG Clock should have been also enabled here. But
		 * it is done currently during board init routne
		 */
		clk = clk_get_sys("pcie1", NULL);
		if (IS_ERR(clk)) {
			pr_err("%s:couldn't get clk for pcie1\n", __func__);
			goto err_irq;
		}
		if (clk_enable(clk)) {
			pr_err("%s:couldn't enable clk for pcie1\n", __func__);
			goto err_irq;
		}
	} else if (pdev->id == 2) {
		/*
		 * Ideally CFG Clock should have been also enabled here. But
		 * it is done currently during board init routne
		 */
		clk = clk_get_sys("pcie2", NULL);
		if (IS_ERR(clk)) {
			pr_err("%s:couldn't get clk for pcie2\n", __func__);
			goto err_irq;
		}
		if (clk_enable(clk)) {
			pr_err("%s:couldn't enable clk for pcie2\n", __func__);
			goto err_irq;
		}
	}
	spear13xx_pcie_device_init(config);

	return 0;
err_irq:
	free_irq(irq, NULL);
err_iounmap:
	iounmap(config->va_dbi_base);
err_iounmap_app:
	iounmap(config->va_app_base);
err_kzalloc:
	kfree(target);
err_rel_res:
	release_mem_region(res1->start, resource_size(res1));
err_rel_res0:
	release_mem_region(res0->start, resource_size(res0));
	return status;
}

static int __devexit spear_pcie_gadget_remove(struct platform_device *pdev)
{
	struct resource *res0, *res1;
	static struct pcie_gadget_target *target;
	struct spear_pcie_gadget_config *config;
	int irq;

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	irq = platform_get_irq(pdev, 0);
	target = dev_get_drvdata(&pdev->dev);
	config = &target->config;

	free_irq(irq, NULL);
	iounmap(config->va_dbi_base);
	iounmap(config->va_app_base);
	release_mem_region(res1->start, resource_size(res1));
	release_mem_region(res0->start, resource_size(res0));
	configfs_unregister_subsystem(&target->subsys);
	kfree(target);

	return 0;
}

static void spear_pcie_gadget_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver spear_pcie_gadget_driver = {
	.probe = spear_pcie_gadget_probe,
	.remove = spear_pcie_gadget_remove,
	.shutdown = spear_pcie_gadget_shutdown,
	.driver = {
		.name = "pcie-gadget-spear",
		.bus = &platform_bus_type
	},
};

static int __init spear_pcie_gadget_init(void)
{
	return platform_driver_register(&spear_pcie_gadget_driver);
}
module_init(spear_pcie_gadget_init);

static void __exit spear_pcie_gadget_exit(void)
{
	platform_driver_unregister(&spear_pcie_gadget_driver);
}
module_exit(spear_pcie_gadget_exit);

MODULE_ALIAS("pcie-gadget-spear");
MODULE_AUTHOR("Pratyush Anand");
MODULE_LICENSE("GPL");

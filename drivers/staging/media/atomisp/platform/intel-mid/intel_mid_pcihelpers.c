#include <linux/export.h>
#include <linux/pci.h>
#include <linux/pm_qos.h>
#include <linux/delay.h>

/* G-Min addition: "platform_is()" lives in intel_mid_pm.h in the MCG
 * tree, but it's just platform ID info and we don't want to pull in
 * the whole SFI-based PM architecture.
 */
#define INTEL_ATOM_MRST 0x26
#define INTEL_ATOM_MFLD 0x27
#define INTEL_ATOM_CLV 0x35
#define INTEL_ATOM_MRFLD 0x4a
#define INTEL_ATOM_BYT 0x37
#define INTEL_ATOM_MOORFLD 0x5a
#define INTEL_ATOM_CHT 0x4c
static inline int platform_is(u8 model)
{
	return (boot_cpu_data.x86_model == model);
}

#include "../../include/asm/intel_mid_pcihelpers.h"

/* Unified message bus read/write operation */
static DEFINE_SPINLOCK(msgbus_lock);

static struct pci_dev *pci_root;
static struct pm_qos_request pm_qos;

#define DW_I2C_NEED_QOS	(platform_is(INTEL_ATOM_BYT))

static int intel_mid_msgbus_init(void)
{
	pci_root = pci_get_bus_and_slot(0, PCI_DEVFN(0, 0));
	if (!pci_root) {
		pr_err("%s: Error: msgbus PCI handle NULL\n", __func__);
		return -ENODEV;
	}

	if (DW_I2C_NEED_QOS) {
		pm_qos_add_request(&pm_qos,
			PM_QOS_CPU_DMA_LATENCY,
			PM_QOS_DEFAULT_VALUE);
	}
	return 0;
}
fs_initcall(intel_mid_msgbus_init);

u32 intel_mid_msgbus_read32_raw(u32 cmd)
{
	unsigned long irq_flags;
	u32 data;

	spin_lock_irqsave(&msgbus_lock, irq_flags);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
	pci_read_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, &data);
	spin_unlock_irqrestore(&msgbus_lock, irq_flags);

	return data;
}
EXPORT_SYMBOL(intel_mid_msgbus_read32_raw);

/*
 * GU: this function is only used by the VISA and 'VXD' drivers.
 */
u32 intel_mid_msgbus_read32_raw_ext(u32 cmd, u32 cmd_ext)
{
	unsigned long irq_flags;
	u32 data;

	spin_lock_irqsave(&msgbus_lock, irq_flags);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_EXT_REG, cmd_ext);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
	pci_read_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, &data);
	spin_unlock_irqrestore(&msgbus_lock, irq_flags);

	return data;
}
EXPORT_SYMBOL(intel_mid_msgbus_read32_raw_ext);

void intel_mid_msgbus_write32_raw(u32 cmd, u32 data)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&msgbus_lock, irq_flags);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, data);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
	spin_unlock_irqrestore(&msgbus_lock, irq_flags);
}
EXPORT_SYMBOL(intel_mid_msgbus_write32_raw);

/*
 * GU: this function is only used by the VISA and 'VXD' drivers.
 */
void intel_mid_msgbus_write32_raw_ext(u32 cmd, u32 cmd_ext, u32 data)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&msgbus_lock, irq_flags);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, data);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_EXT_REG, cmd_ext);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
	spin_unlock_irqrestore(&msgbus_lock, irq_flags);
}
EXPORT_SYMBOL(intel_mid_msgbus_write32_raw_ext);

u32 intel_mid_msgbus_read32(u8 port, u32 addr)
{
	unsigned long irq_flags;
	u32 data;
	u32 cmd;
	u32 cmdext;

	cmd = (PCI_ROOT_MSGBUS_READ << 24) | (port << 16) |
		((addr & 0xff) << 8) | PCI_ROOT_MSGBUS_DWORD_ENABLE;
	cmdext = addr & 0xffffff00;

	spin_lock_irqsave(&msgbus_lock, irq_flags);

	if (cmdext) {
		/* This resets to 0 automatically, no need to write 0 */
		pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_EXT_REG,
					cmdext);
	}

	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
	pci_read_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, &data);
	spin_unlock_irqrestore(&msgbus_lock, irq_flags);

	return data;
}
EXPORT_SYMBOL(intel_mid_msgbus_read32);

void intel_mid_msgbus_write32(u8 port, u32 addr, u32 data)
{
	unsigned long irq_flags;
	u32 cmd;
	u32 cmdext;

	cmd = (PCI_ROOT_MSGBUS_WRITE << 24) | (port << 16) |
		((addr & 0xFF) << 8) | PCI_ROOT_MSGBUS_DWORD_ENABLE;
	cmdext = addr & 0xffffff00;

	spin_lock_irqsave(&msgbus_lock, irq_flags);
	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_DATA_REG, data);

	if (cmdext) {
		/* This resets to 0 automatically, no need to write 0 */
		pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_EXT_REG,
					cmdext);
	}

	pci_write_config_dword(pci_root, PCI_ROOT_MSGBUS_CTRL_REG, cmd);
	spin_unlock_irqrestore(&msgbus_lock, irq_flags);
}
EXPORT_SYMBOL(intel_mid_msgbus_write32);

/* called only from where is later then fs_initcall */
u32 intel_mid_soc_stepping(void)
{
	return pci_root->revision;
}
EXPORT_SYMBOL(intel_mid_soc_stepping);

// SPDX-License-Identifier: GPL-2.0
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
/* synchronization for sharing the I2C controller */
#define PUNIT_PORT	0x04
#define PUNIT_DOORBELL_OPCODE	(0xE0)
#define PUNIT_DOORBELL_REG	(0x0)
#ifndef CSTATE_EXIT_LATENCY
#define CSTATE_EXIT_LATENCY_C1 1
#endif
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

static bool is_south_complex_device(struct pci_dev *dev)
{
	unsigned int base_class = dev->class >> 16;
	unsigned int sub_class  = (dev->class & SUB_CLASS_MASK) >> 8;

	/* other than camera, pci bridges and display,
	 * everything else are south complex devices.
	 */
	if (((base_class == PCI_BASE_CLASS_MULTIMEDIA) &&
	     (sub_class == ISP_SUB_CLASS)) ||
	    (base_class == PCI_BASE_CLASS_BRIDGE) ||
	    ((base_class == PCI_BASE_CLASS_DISPLAY) && !sub_class))
		return false;
	else
		return true;
}

/* In BYT platform, d3_delay for internal south complex devices,
 * they are not subject to 10 ms d3 to d0 delay required by pci spec.
 */
static void pci_d3_delay_fixup(struct pci_dev *dev)
{
	if (platform_is(INTEL_ATOM_BYT) ||
		platform_is(INTEL_ATOM_CHT)) {
		/* All internal devices are in bus 0. */
		if (dev->bus->number == 0 && is_south_complex_device(dev)) {
			dev->d3_delay = INTERNAL_PCI_PM_D3_WAIT;
			dev->d3cold_delay = INTERNAL_PCI_PM_D3_WAIT;
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pci_d3_delay_fixup);

#define PUNIT_SEMAPHORE	(platform_is(INTEL_ATOM_BYT) ? 0x7 : 0x10E)
#define GET_SEM() (intel_mid_msgbus_read32(PUNIT_PORT, PUNIT_SEMAPHORE) & 0x1)

static void reset_semaphore(void)
{
	u32 data;

	data = intel_mid_msgbus_read32(PUNIT_PORT, PUNIT_SEMAPHORE);
	smp_mb();
	data = data & 0xfffffffc;
	intel_mid_msgbus_write32(PUNIT_PORT, PUNIT_SEMAPHORE, data);
	smp_mb();

}

int intel_mid_dw_i2c_acquire_ownership(void)
{
	u32 ret = 0;
	u32 data = 0; /* data sent to PUNIT */
	u32 cmd;
	u32 cmdext;
	int timeout = 1000;

	if (DW_I2C_NEED_QOS)
		pm_qos_update_request(&pm_qos, CSTATE_EXIT_LATENCY_C1 - 1);

	/*
	 * We need disable irq. Otherwise, the main thread
	 * might be preempted and the other thread jumps to
	 * disable irq for a long time. Another case is
	 * some irq handlers might trigger power voltage change
	 */
	BUG_ON(irqs_disabled());
	local_irq_disable();

	/* host driver writes 0x2 to side band register 0x7 */
	intel_mid_msgbus_write32(PUNIT_PORT, PUNIT_SEMAPHORE, 0x2);
	smp_mb();

	/* host driver sends 0xE0 opcode to PUNIT and writes 0 register */
	cmd = (PUNIT_DOORBELL_OPCODE << 24) | (PUNIT_PORT << 16) |
	((PUNIT_DOORBELL_REG & 0xFF) << 8) | PCI_ROOT_MSGBUS_DWORD_ENABLE;
	cmdext = PUNIT_DOORBELL_REG & 0xffffff00;

	if (cmdext)
		intel_mid_msgbus_write32_raw_ext(cmd, cmdext, data);
	else
		intel_mid_msgbus_write32_raw(cmd, data);

	/* host driver waits for bit 0 to be set in side band 0x7 */
	while (GET_SEM() != 0x1) {
		udelay(100);
		timeout--;
		if (timeout <= 0) {
			pr_err("Timeout: semaphore timed out, reset sem\n");
			ret = -ETIMEDOUT;
			reset_semaphore();
			/*Delay 1ms in case race with punit*/
			udelay(1000);
			if (GET_SEM() != 0) {
				/*Reset again as kernel might race with punit*/
				reset_semaphore();
			}
			pr_err("PUNIT SEM: %d\n",
					intel_mid_msgbus_read32(PUNIT_PORT,
						PUNIT_SEMAPHORE));
			local_irq_enable();

			if (DW_I2C_NEED_QOS) {
				pm_qos_update_request(&pm_qos,
					 PM_QOS_DEFAULT_VALUE);
			}

			return ret;
		}
	}
	smp_mb();

	return ret;
}
EXPORT_SYMBOL(intel_mid_dw_i2c_acquire_ownership);

int intel_mid_dw_i2c_release_ownership(void)
{
	reset_semaphore();
	local_irq_enable();

	if (DW_I2C_NEED_QOS)
		pm_qos_update_request(&pm_qos, PM_QOS_DEFAULT_VALUE);

	return 0;
}
EXPORT_SYMBOL(intel_mid_dw_i2c_release_ownership);

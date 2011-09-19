/*
 * mrst/pmu.c - driver for MRST Power Management Unit
 *
 * Copyright (c) 2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/cpuidle.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/sfi.h>
#include <asm/intel_scu_ipc.h>
#include "pmu.h"

#define IPCMSG_FW_REVISION	0xF4

struct mrst_device {
	u16 pci_dev_num;	/* DEBUG only */
	u16 lss;
	u16 latest_request;
	unsigned int pci_state_counts[PCI_D3cold + 1]; /* DEBUG only */
};

/*
 * comlete list of MRST PCI devices
 */
static struct mrst_device mrst_devs[] = {
/*  0 */ { 0x0800, LSS_SPI0 },		/* Moorestown SPI Ctrl 0 */
/*  1 */ { 0x0801, LSS_SPI1 },		/* Moorestown SPI Ctrl 1 */
/*  2 */ { 0x0802, LSS_I2C0 },		/* Moorestown I2C 0 */
/*  3 */ { 0x0803, LSS_I2C1 },		/* Moorestown I2C 1 */
/*  4 */ { 0x0804, LSS_I2C2 },		/* Moorestown I2C 2 */
/*  5 */ { 0x0805, LSS_KBD },		/* Moorestown Keyboard Ctrl */
/*  6 */ { 0x0806, LSS_USB_HC },	/* Moorestown USB Ctrl */
/*  7 */ { 0x0807, LSS_SD_HC0 },	/* Moorestown SD Host Ctrl 0 */
/*  8 */ { 0x0808, LSS_SD_HC1 },	/* Moorestown SD Host Ctrl 1 */
/*  9 */ { 0x0809, LSS_NAND },		/* Moorestown NAND Ctrl */
/* 10 */ { 0x080a, LSS_AUDIO },		/* Moorestown Audio Ctrl */
/* 11 */ { 0x080b, LSS_IMAGING },	/* Moorestown ISP */
/* 12 */ { 0x080c, LSS_SECURITY },	/* Moorestown Security Controller */
/* 13 */ { 0x080d, LSS_DISPLAY },	/* Moorestown External Displays */
/* 14 */ { 0x080e, 0 },			/* Moorestown SCU IPC */
/* 15 */ { 0x080f, LSS_GPIO },		/* Moorestown GPIO Controller */
/* 16 */ { 0x0810, 0 },			/* Moorestown Power Management Unit */
/* 17 */ { 0x0811, LSS_USB_OTG },	/* Moorestown OTG Ctrl */
/* 18 */ { 0x0812, LSS_SPI2 },		/* Moorestown SPI Ctrl 2 */
/* 19 */ { 0x0813, 0 },			/* Moorestown SC DMA */
/* 20 */ { 0x0814, LSS_AUDIO_LPE },	/* Moorestown LPE DMA */
/* 21 */ { 0x0815, LSS_AUDIO_SSP },	/* Moorestown SSP0 */

/* 22 */ { 0x084F, LSS_SD_HC2 },	/* Moorestown SD Host Ctrl 2 */

/* 23 */ { 0x4102, 0 },			/* Lincroft */
/* 24 */ { 0x4110, 0 },			/* Lincroft */
};

/* n.b. We ignore PCI-id 0x815 in LSS9 b/c MeeGo has no driver for it */
static u16 mrst_lss9_pci_ids[] = {0x080a, 0x0814, 0};
static u16 mrst_lss10_pci_ids[] = {0x0800, 0x0801, 0x0802, 0x0803,
					0x0804, 0x0805, 0x080f, 0};

/* handle concurrent SMP invokations of pmu_pci_set_power_state() */
static spinlock_t mrst_pmu_power_state_lock;

static unsigned int wake_counters[MRST_NUM_LSS];	/* DEBUG only */
static unsigned int pmu_irq_stats[INT_INVALID + 1];	/* DEBUG only */

static int graphics_is_off;
static int lss_s0i3_enabled;
static bool mrst_pmu_s0i3_enable;

/*  debug counters */
static u32 pmu_wait_ready_calls;
static u32 pmu_wait_ready_udelays;
static u32 pmu_wait_ready_udelays_max;
static u32 pmu_wait_done_calls;
static u32 pmu_wait_done_udelays;
static u32 pmu_wait_done_udelays_max;
static u32 pmu_set_power_state_entry;
static u32 pmu_set_power_state_send_cmd;

static struct mrst_device *pci_id_2_mrst_dev(u16 pci_dev_num)
{
	int index = 0;

	if ((pci_dev_num >= 0x0800) && (pci_dev_num <= 0x815))
		index = pci_dev_num - 0x800;
	else if (pci_dev_num == 0x084F)
		index = 22;
	else if (pci_dev_num == 0x4102)
		index = 23;
	else if (pci_dev_num == 0x4110)
		index = 24;

	if (pci_dev_num != mrst_devs[index].pci_dev_num) {
		WARN_ONCE(1, FW_BUG "Unknown PCI device 0x%04X\n", pci_dev_num);
		return 0;
	}

	return &mrst_devs[index];
}

/**
 * mrst_pmu_validate_cstates
 * @dev: cpuidle_device
 *
 * Certain states are not appropriate for governor to pick in some cases.
 * This function will be called as cpuidle_device's prepare callback and
 * thus tells governor to ignore such states when selecting the next state
 * to enter.
 */

#define IDLE_STATE4_IS_C6	4
#define IDLE_STATE5_IS_S0I3	5

int mrst_pmu_invalid_cstates(void)
{
	int cpu = smp_processor_id();

	/*
	 * Demote to C4 if the PMU is busy.
	 * Since LSS changes leave the busy bit clear...
	 * busy means either the PMU is waiting for an ACK-C6 that
	 * isn't coming due to an MWAIT that returned immediately;
	 * or we returned from S0i3 successfully, and the PMU
	 * is not done sending us interrupts.
	 */
	if (pmu_read_busy_status())
		return 1 << IDLE_STATE4_IS_C6 | 1 << IDLE_STATE5_IS_S0I3;

	/*
	 * Disallow S0i3 if: PMU is not initialized, or CPU1 is active,
	 * or if device LSS is insufficient, or the GPU is active,
	 * or if it has been explicitly disabled.
	 */
	if (!pmu_reg || !cpumask_equal(cpu_online_mask, cpumask_of(cpu)) ||
	    !lss_s0i3_enabled || !graphics_is_off || !mrst_pmu_s0i3_enable)
		return 1 << IDLE_STATE5_IS_S0I3;
	else
		return 0;
}

/*
 * pmu_update_wake_counters(): read PM_WKS, update wake_counters[]
 * DEBUG only.
 */
static void pmu_update_wake_counters(void)
{
	int lss;
	u32 wake_status;

	wake_status = pmu_read_wks();

	for (lss = 0; lss < MRST_NUM_LSS; ++lss) {
		if (wake_status & (1 << lss))
			wake_counters[lss]++;
	}
}

int mrst_pmu_s0i3_entry(void)
{
	int status;

	/* Clear any possible error conditions */
	pmu_write_ics(0x300);

	/* set wake control to current D-states */
	pmu_write_wssc(S0I3_SSS_TARGET);

	status = mrst_s0i3_entry(PM_S0I3_COMMAND, &pmu_reg->pm_cmd);
	pmu_update_wake_counters();
	return status;
}

/* poll for maximum of 5ms for busy bit to clear */
static int pmu_wait_ready(void)
{
	int udelays;

	pmu_wait_ready_calls++;

	for (udelays = 0; udelays < 500; ++udelays) {
		if (udelays > pmu_wait_ready_udelays_max)
			pmu_wait_ready_udelays_max = udelays;

		if (pmu_read_busy_status() == 0)
			return 0;

		udelay(10);
		pmu_wait_ready_udelays++;
	}

	/*
	 * if this fires, observe
	 * /sys/kernel/debug/mrst_pmu_wait_ready_calls
	 * /sys/kernel/debug/mrst_pmu_wait_ready_udelays
	 */
	WARN_ONCE(1, "SCU not ready for 5ms");
	return -EBUSY;
}
/* poll for maximum of 50ms us for busy bit to clear */
static int pmu_wait_done(void)
{
	int udelays;

	pmu_wait_done_calls++;

	for (udelays = 0; udelays < 500; ++udelays) {
		if (udelays > pmu_wait_done_udelays_max)
			pmu_wait_done_udelays_max = udelays;

		if (pmu_read_busy_status() == 0)
			return 0;

		udelay(100);
		pmu_wait_done_udelays++;
	}

	/*
	 * if this fires, observe
	 * /sys/kernel/debug/mrst_pmu_wait_done_calls
	 * /sys/kernel/debug/mrst_pmu_wait_done_udelays
	 */
	WARN_ONCE(1, "SCU not done for 50ms");
	return -EBUSY;
}

u32 mrst_pmu_msi_is_disabled(void)
{
	return pmu_msi_is_disabled();
}

void mrst_pmu_enable_msi(void)
{
	pmu_msi_enable();
}

/**
 * pmu_irq - pmu driver interrupt handler
 * Context: interrupt context
 */
static irqreturn_t pmu_irq(int irq, void *dummy)
{
	union pmu_pm_ics pmu_ics;

	pmu_ics.value = pmu_read_ics();

	if (!pmu_ics.bits.pending)
		return IRQ_NONE;

	switch (pmu_ics.bits.cause) {
	case INT_SPURIOUS:
	case INT_CMD_DONE:
	case INT_CMD_ERR:
	case INT_WAKE_RX:
	case INT_SS_ERROR:
	case INT_S0IX_MISS:
	case INT_NO_ACKC6:
		pmu_irq_stats[pmu_ics.bits.cause]++;
		break;
	default:
		pmu_irq_stats[INT_INVALID]++;
	}

	pmu_write_ics(pmu_ics.value); /* Clear pending interrupt */

	return IRQ_HANDLED;
}

/*
 * Translate PCI power management to MRST LSS D-states
 */
static int pci_2_mrst_state(int lss, pci_power_t pci_state)
{
	switch (pci_state) {
	case PCI_D0:
		if (SSMSK(D0i1, lss) & D0I1_ACG_SSS_TARGET)
			return D0i1;
		else
			return D0;
	case PCI_D1:
		return D0i1;
	case PCI_D2:
		return D0i2;
	case PCI_D3hot:
	case PCI_D3cold:
		return D0i3;
	default:
		WARN(1, "pci_state %d\n", pci_state);
		return 0;
	}
}

static int pmu_issue_command(u32 pm_ssc)
{
	union pmu_pm_set_cfg_cmd_t command;

	if (pmu_read_busy_status()) {
		pr_debug("pmu is busy, Operation not permitted\n");
		return -1;
	}

	/*
	 * enable interrupts in PMU so that interrupts are
	 * propagated when ioc bit for a particular set
	 * command is set
	 */

	pmu_irq_enable();

	/* Configure the sub systems for pmu2 */

	pmu_write_ssc(pm_ssc);

	/*
	 * Send the set config command for pmu its configured
	 * for mode CM_IMMEDIATE & hence with No Trigger
	 */

	command.pmu2_params.d_param.cfg_mode = CM_IMMEDIATE;
	command.pmu2_params.d_param.cfg_delay = 0;
	command.pmu2_params.d_param.rsvd = 0;

	/* construct the command to send SET_CFG to particular PMU */
	command.pmu2_params.d_param.cmd = SET_CFG_CMD;
	command.pmu2_params.d_param.ioc = 0;
	command.pmu2_params.d_param.mode_id = 0;
	command.pmu2_params.d_param.sys_state = SYS_STATE_S0I0;

	/* write the value of PM_CMD into particular PMU */
	pr_debug("pmu command being written %x\n",
			command.pmu_pm_set_cfg_cmd_value);

	pmu_write_cmd(command.pmu_pm_set_cfg_cmd_value);

	return 0;
}

static u16 pmu_min_lss_pci_req(u16 *ids, u16 pci_state)
{
	u16 existing_request;
	int i;

	for (i = 0; ids[i]; ++i) {
		struct mrst_device *mrst_dev;

		mrst_dev = pci_id_2_mrst_dev(ids[i]);
		if (unlikely(!mrst_dev))
			continue;

		existing_request = mrst_dev->latest_request;
		if (existing_request < pci_state)
			pci_state = existing_request;
	}
	return pci_state;
}

/**
 * pmu_pci_set_power_state - Callback function is used by all the PCI devices
 *			for a platform  specific device power on/shutdown.
 */

int pmu_pci_set_power_state(struct pci_dev *pdev, pci_power_t pci_state)
{
	u32 old_sss, new_sss;
	int status = 0;
	struct mrst_device *mrst_dev;

	pmu_set_power_state_entry++;

	BUG_ON(pdev->vendor != PCI_VENDOR_ID_INTEL);
	BUG_ON(pci_state < PCI_D0 || pci_state > PCI_D3cold);

	mrst_dev = pci_id_2_mrst_dev(pdev->device);
	if (unlikely(!mrst_dev))
		return -ENODEV;

	mrst_dev->pci_state_counts[pci_state]++;	/* count invocations */

	/* PMU driver calls self as part of PCI initialization, ignore */
	if (pdev->device == PCI_DEV_ID_MRST_PMU)
		return 0;

	BUG_ON(!pmu_reg); /* SW bug if called before initialized */

	spin_lock(&mrst_pmu_power_state_lock);

	if (pdev->d3_delay) {
		dev_dbg(&pdev->dev, "d3_delay %d, should be 0\n",
			pdev->d3_delay);
		pdev->d3_delay = 0;
	}
	/*
	 * If Lincroft graphics, simply remember state
	 */
	if ((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY
		&& !((pdev->class & PCI_SUB_CLASS_MASK) >> 8)) {
		if (pci_state == PCI_D0)
			graphics_is_off = 0;
		else
			graphics_is_off = 1;
		goto ret;
	}

	if (!mrst_dev->lss)
		goto ret;	/* device with no LSS */

	if (mrst_dev->latest_request == pci_state)
		goto ret;	/* no change */

	mrst_dev->latest_request = pci_state;	/* record latest request */

	/*
	 * LSS9 and LSS10 contain multiple PCI devices.
	 * Use the lowest numbered (highest power) state in the LSS
	 */
	if (mrst_dev->lss == 9)
		pci_state = pmu_min_lss_pci_req(mrst_lss9_pci_ids, pci_state);
	else if (mrst_dev->lss == 10)
		pci_state = pmu_min_lss_pci_req(mrst_lss10_pci_ids, pci_state);

	status = pmu_wait_ready();
	if (status)
		goto ret;

	old_sss = pmu_read_sss();
	new_sss = old_sss & ~SSMSK(3, mrst_dev->lss);
	new_sss |= SSMSK(pci_2_mrst_state(mrst_dev->lss, pci_state),
			mrst_dev->lss);

	if (new_sss == old_sss)
		goto ret;	/* nothing to do */

	pmu_set_power_state_send_cmd++;

	status = pmu_issue_command(new_sss);

	if (unlikely(status != 0)) {
		dev_err(&pdev->dev, "Failed to Issue a PM command\n");
		goto ret;
	}

	if (pmu_wait_done())
		goto ret;

	lss_s0i3_enabled =
	((pmu_read_sss() & S0I3_SSS_TARGET) == S0I3_SSS_TARGET);
ret:
	spin_unlock(&mrst_pmu_power_state_lock);
	return status;
}

#ifdef CONFIG_DEBUG_FS
static char *d0ix_names[] = {"D0", "D0i1", "D0i2", "D0i3"};

static inline const char *d0ix_name(int state)
{
	return d0ix_names[(int) state];
}

static int debug_mrst_pmu_show(struct seq_file *s, void *unused)
{
	struct pci_dev *pdev = NULL;
	u32 cur_pmsss;
	int lss;

	seq_printf(s, "0x%08X D0I1_ACG_SSS_TARGET\n", D0I1_ACG_SSS_TARGET);

	cur_pmsss = pmu_read_sss();

	seq_printf(s, "0x%08X S0I3_SSS_TARGET\n", S0I3_SSS_TARGET);

	seq_printf(s, "0x%08X Current SSS ", cur_pmsss);
	seq_printf(s, lss_s0i3_enabled ? "\n" : "[BLOCKS s0i3]\n");

	if (cpumask_equal(cpu_online_mask, cpumask_of(0)))
		seq_printf(s, "cpu0 is only cpu online\n");
	else
		seq_printf(s, "cpu0 is NOT only cpu online [BLOCKS S0i3]\n");

	seq_printf(s, "GFX: %s\n", graphics_is_off ? "" : "[BLOCKS s0i3]");


	for_each_pci_dev(pdev) {
		int pos;
		u16 pmcsr;
		struct mrst_device *mrst_dev;
		int i;

		mrst_dev = pci_id_2_mrst_dev(pdev->device);

		seq_printf(s, "%s %04x/%04X %-16.16s ",
			dev_name(&pdev->dev),
			pdev->vendor, pdev->device,
			dev_driver_string(&pdev->dev));

		if (unlikely (!mrst_dev)) {
			seq_printf(s, " UNKNOWN\n");
			continue;
		}

		if (mrst_dev->lss)
			seq_printf(s, "LSS %2d %-4s ", mrst_dev->lss,
				d0ix_name(((cur_pmsss >>
					(mrst_dev->lss * 2)) & 0x3)));
		else
			seq_printf(s, "            ");

		/* PCI PM config space setting */
		pos = pci_find_capability(pdev, PCI_CAP_ID_PM);
		if (pos != 0) {
			pci_read_config_word(pdev, pos + PCI_PM_CTRL, &pmcsr);
		seq_printf(s, "PCI-%-4s",
			pci_power_name(pmcsr & PCI_PM_CTRL_STATE_MASK));
		} else {
			seq_printf(s, "        ");
		}

		seq_printf(s, " %s ", pci_power_name(mrst_dev->latest_request));
		for (i = 0; i <= PCI_D3cold; ++i)
			seq_printf(s, "%d ", mrst_dev->pci_state_counts[i]);

		if (mrst_dev->lss) {
			unsigned int lssmask;

			lssmask = SSMSK(D0i3, mrst_dev->lss);

			if ((lssmask & S0I3_SSS_TARGET) &&
				((lssmask & cur_pmsss) !=
					(lssmask & S0I3_SSS_TARGET)))
						seq_printf(s , "[BLOCKS s0i3]");
		}

		seq_printf(s, "\n");
	}
	seq_printf(s, "Wake Counters:\n");
	for (lss = 0; lss < MRST_NUM_LSS; ++lss)
		seq_printf(s, "LSS%d %d\n", lss, wake_counters[lss]);

	seq_printf(s, "Interrupt Counters:\n");
	seq_printf(s,
		"INT_SPURIOUS \t%8u\n" "INT_CMD_DONE \t%8u\n"
		"INT_CMD_ERR  \t%8u\n" "INT_WAKE_RX  \t%8u\n"
		"INT_SS_ERROR \t%8u\n" "INT_S0IX_MISS\t%8u\n"
		"INT_NO_ACKC6 \t%8u\n" "INT_INVALID  \t%8u\n",
		pmu_irq_stats[INT_SPURIOUS], pmu_irq_stats[INT_CMD_DONE],
		pmu_irq_stats[INT_CMD_ERR], pmu_irq_stats[INT_WAKE_RX],
		pmu_irq_stats[INT_SS_ERROR], pmu_irq_stats[INT_S0IX_MISS],
		pmu_irq_stats[INT_NO_ACKC6], pmu_irq_stats[INT_INVALID]);

	seq_printf(s, "mrst_pmu_wait_ready_calls          %8d\n",
			pmu_wait_ready_calls);
	seq_printf(s, "mrst_pmu_wait_ready_udelays        %8d\n",
			pmu_wait_ready_udelays);
	seq_printf(s, "mrst_pmu_wait_ready_udelays_max    %8d\n",
			pmu_wait_ready_udelays_max);
	seq_printf(s, "mrst_pmu_wait_done_calls           %8d\n",
			pmu_wait_done_calls);
	seq_printf(s, "mrst_pmu_wait_done_udelays         %8d\n",
			pmu_wait_done_udelays);
	seq_printf(s, "mrst_pmu_wait_done_udelays_max     %8d\n",
			pmu_wait_done_udelays_max);
	seq_printf(s, "mrst_pmu_set_power_state_entry     %8d\n",
			pmu_set_power_state_entry);
	seq_printf(s, "mrst_pmu_set_power_state_send_cmd  %8d\n",
			pmu_set_power_state_send_cmd);
	seq_printf(s, "SCU busy: %d\n", pmu_read_busy_status());

	return 0;
}

static int debug_mrst_pmu_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_mrst_pmu_show, NULL);
}

static const struct file_operations devices_state_operations = {
	.open		= debug_mrst_pmu_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif	/* DEBUG_FS */

/*
 * Validate SCU PCI shim PCI vendor capability byte
 * against LSS hard-coded in mrst_devs[] above.
 * DEBUG only.
 */
static void pmu_scu_firmware_debug(void)
{
	struct pci_dev *pdev = NULL;

	for_each_pci_dev(pdev) {
		struct mrst_device *mrst_dev;
		u8 pci_config_lss;
		int pos;

		mrst_dev = pci_id_2_mrst_dev(pdev->device);
		if (unlikely(!mrst_dev)) {
			printk(KERN_ERR FW_BUG "pmu: Unknown "
				"PCI device 0x%04X\n", pdev->device);
			continue;
		}

		if (mrst_dev->lss == 0)
			continue;	 /* no LSS in our table */

		pos = pci_find_capability(pdev, PCI_CAP_ID_VNDR);
		if (!pos != 0) {
			printk(KERN_ERR FW_BUG "pmu: 0x%04X "
				"missing PCI Vendor Capability\n",
				pdev->device);
			continue;
		}
		pci_read_config_byte(pdev, pos + 4, &pci_config_lss);
		if (!(pci_config_lss & PCI_VENDOR_CAP_LOG_SS_MASK)) {
			printk(KERN_ERR FW_BUG "pmu: 0x%04X "
				"invalid PCI Vendor Capability 0x%x "
				" expected LSS 0x%X\n",
				pdev->device, pci_config_lss, mrst_dev->lss);
			continue;
		}
		pci_config_lss &= PCI_VENDOR_CAP_LOG_ID_MASK;

		if (mrst_dev->lss == pci_config_lss)
			continue;

		printk(KERN_ERR FW_BUG "pmu: 0x%04X LSS = %d, expected %d\n",
			pdev->device, pci_config_lss, mrst_dev->lss);
	}
}

/**
 * pmu_probe
 */
static int __devinit pmu_probe(struct pci_dev *pdev,
				   const struct pci_device_id *pci_id)
{
	int ret;
	struct mrst_pmu_reg *pmu;

	/* Init the device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to Enable PCI device\n");
		return ret;
	}

	ret = pci_request_regions(pdev, MRST_PMU_DRV_NAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, aborting\n");
		goto out_err1;
	}

	/* Map the memory of PMU reg base */
	pmu = pci_iomap(pdev, 0, 0);
	if (!pmu) {
		dev_err(&pdev->dev, "Unable to map the PMU address space\n");
		ret = -ENOMEM;
		goto out_err2;
	}

#ifdef CONFIG_DEBUG_FS
	/* /sys/kernel/debug/mrst_pmu */
	(void) debugfs_create_file("mrst_pmu", S_IFREG | S_IRUGO,
				NULL, NULL, &devices_state_operations);
#endif
	pmu_reg = pmu;	/* success */

	if (request_irq(pdev->irq, pmu_irq, 0, MRST_PMU_DRV_NAME, NULL)) {
		dev_err(&pdev->dev, "Registering isr has failed\n");
		ret = -1;
		goto out_err3;
	}

	pmu_scu_firmware_debug();

	pmu_write_wkc(S0I3_WAKE_SOURCES);	/* Enable S0i3 wakeup sources */

	pmu_wait_ready();

	pmu_write_ssc(D0I1_ACG_SSS_TARGET);	/* Enable Auto-Clock_Gating */
	pmu_write_cmd(0x201);

	spin_lock_init(&mrst_pmu_power_state_lock);

	/* Enable the hardware interrupt */
	pmu_irq_enable();
	return 0;

out_err3:
	free_irq(pdev->irq, NULL);
	pci_iounmap(pdev, pmu_reg);
	pmu_reg = NULL;
out_err2:
	pci_release_region(pdev, 0);
out_err1:
	pci_disable_device(pdev);
	return ret;
}

static void __devexit pmu_remove(struct pci_dev *pdev)
{
	dev_err(&pdev->dev, "Mid PM pmu_remove called\n");

	/* Freeing up the irq */
	free_irq(pdev->irq, NULL);

	pci_iounmap(pdev, pmu_reg);
	pmu_reg = NULL;

	/* disable the current PCI device */
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
}

static DEFINE_PCI_DEVICE_TABLE(pmu_pci_ids) = {
	{ PCI_VDEVICE(INTEL, PCI_DEV_ID_MRST_PMU), 0 },
	{ }
};

MODULE_DEVICE_TABLE(pci, pmu_pci_ids);

static struct pci_driver driver = {
	.name = MRST_PMU_DRV_NAME,
	.id_table = pmu_pci_ids,
	.probe = pmu_probe,
	.remove = __devexit_p(pmu_remove),
};

/**
 * pmu_pci_register - register the PMU driver as PCI device
 */
static int __init pmu_pci_register(void)
{
	return pci_register_driver(&driver);
}

/* Register and probe via fs_initcall() to preceed device_initcall() */
fs_initcall(pmu_pci_register);

static void __exit mid_pci_cleanup(void)
{
	pci_unregister_driver(&driver);
}

static int ia_major;
static int ia_minor;

static int pmu_sfi_parse_oem(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;

	sb = (struct sfi_table_simple *)table;
	ia_major = (sb->pentry[1] >> 0) & 0xFFFF;
	ia_minor = (sb->pentry[1] >> 16) & 0xFFFF;
	printk(KERN_INFO "mrst_pmu: IA FW version v%x.%x\n",
		ia_major, ia_minor);

	return 0;
}

static int __init scu_fw_check(void)
{
	int ret;
	u32 fw_version;

	if (!pmu_reg)
		return 0;	/* this driver didn't probe-out */

	sfi_table_parse("OEMB", NULL, NULL, pmu_sfi_parse_oem);

	if (ia_major < 0x6005 || ia_minor < 0x1525) {
		WARN(1, "mrst_pmu: IA FW version too old\n");
		return -1;
	}

	ret = intel_scu_ipc_command(IPCMSG_FW_REVISION, 0, NULL, 0,
					&fw_version, 1);

	if (ret) {
		WARN(1, "mrst_pmu: IPC FW version? %d\n", ret);
	} else {
		int scu_major = (fw_version >> 8) & 0xFF;
		int scu_minor = (fw_version >> 0) & 0xFF;

		printk(KERN_INFO "mrst_pmu: firmware v%x\n", fw_version);

		if ((scu_major >= 0xC0) && (scu_minor >= 0x49)) {
			printk(KERN_INFO "mrst_pmu: enabling S0i3\n");
			mrst_pmu_s0i3_enable = true;
		} else {
			WARN(1, "mrst_pmu: S0i3 disabled, old firmware %X.%X",
					scu_major, scu_minor);
		}
	}
	return 0;
}
late_initcall(scu_fw_check);
module_exit(mid_pci_cleanup);

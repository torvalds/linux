// SPDX-License-Identifier: GPL-2.0
/*
 * NVIDIA Tegra xHCI host controller driver
 *
 * Copyright (c) 2014-2020, NVIDIA CORPORATION. All rights reserved.
 * Copyright (C) 2014 Google, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/phy/phy.h>
#include <linux/phy/tegra/xusb.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/usb/otg.h>
#include <linux/usb/phy.h>
#include <linux/usb/role.h>
#include <soc/tegra/pmc.h>

#include "xhci.h"

#define TEGRA_XHCI_SS_HIGH_SPEED 120000000
#define TEGRA_XHCI_SS_LOW_SPEED   12000000

/* FPCI CFG registers */
#define XUSB_CFG_1				0x004
#define  XUSB_IO_SPACE_EN			BIT(0)
#define  XUSB_MEM_SPACE_EN			BIT(1)
#define  XUSB_BUS_MASTER_EN			BIT(2)
#define XUSB_CFG_4				0x010
#define  XUSB_BASE_ADDR_SHIFT			15
#define  XUSB_BASE_ADDR_MASK			0x1ffff
#define XUSB_CFG_16				0x040
#define XUSB_CFG_24				0x060
#define XUSB_CFG_AXI_CFG			0x0f8
#define XUSB_CFG_ARU_C11_CSBRANGE		0x41c
#define XUSB_CFG_ARU_CONTEXT			0x43c
#define XUSB_CFG_ARU_CONTEXT_HS_PLS		0x478
#define XUSB_CFG_ARU_CONTEXT_FS_PLS		0x47c
#define XUSB_CFG_ARU_CONTEXT_HSFS_SPEED		0x480
#define XUSB_CFG_ARU_CONTEXT_HSFS_PP		0x484
#define XUSB_CFG_CSB_BASE_ADDR			0x800

/* FPCI mailbox registers */
/* XUSB_CFG_ARU_MBOX_CMD */
#define  MBOX_DEST_FALC				BIT(27)
#define  MBOX_DEST_PME				BIT(28)
#define  MBOX_DEST_SMI				BIT(29)
#define  MBOX_DEST_XHCI				BIT(30)
#define  MBOX_INT_EN				BIT(31)
/* XUSB_CFG_ARU_MBOX_DATA_IN and XUSB_CFG_ARU_MBOX_DATA_OUT */
#define  CMD_DATA_SHIFT				0
#define  CMD_DATA_MASK				0xffffff
#define  CMD_TYPE_SHIFT				24
#define  CMD_TYPE_MASK				0xff
/* XUSB_CFG_ARU_MBOX_OWNER */
#define  MBOX_OWNER_NONE			0
#define  MBOX_OWNER_FW				1
#define  MBOX_OWNER_SW				2
#define XUSB_CFG_ARU_SMI_INTR			0x428
#define  MBOX_SMI_INTR_FW_HANG			BIT(1)
#define  MBOX_SMI_INTR_EN			BIT(3)

/* IPFS registers */
#define IPFS_XUSB_HOST_MSI_BAR_SZ_0		0x0c0
#define IPFS_XUSB_HOST_MSI_AXI_BAR_ST_0		0x0c4
#define IPFS_XUSB_HOST_MSI_FPCI_BAR_ST_0	0x0c8
#define IPFS_XUSB_HOST_MSI_VEC0_0		0x100
#define IPFS_XUSB_HOST_MSI_EN_VEC0_0		0x140
#define IPFS_XUSB_HOST_CONFIGURATION_0		0x180
#define  IPFS_EN_FPCI				BIT(0)
#define IPFS_XUSB_HOST_FPCI_ERROR_MASKS_0	0x184
#define IPFS_XUSB_HOST_INTR_MASK_0		0x188
#define  IPFS_IP_INT_MASK			BIT(16)
#define IPFS_XUSB_HOST_INTR_ENABLE_0		0x198
#define IPFS_XUSB_HOST_UFPCI_CONFIG_0		0x19c
#define IPFS_XUSB_HOST_CLKGATE_HYSTERESIS_0	0x1bc
#define IPFS_XUSB_HOST_MCCIF_FIFOCTRL_0		0x1dc

#define CSB_PAGE_SELECT_MASK			0x7fffff
#define CSB_PAGE_SELECT_SHIFT			9
#define CSB_PAGE_OFFSET_MASK			0x1ff
#define CSB_PAGE_SELECT(addr)	((addr) >> (CSB_PAGE_SELECT_SHIFT) &	\
				 CSB_PAGE_SELECT_MASK)
#define CSB_PAGE_OFFSET(addr)	((addr) & CSB_PAGE_OFFSET_MASK)

/* Falcon CSB registers */
#define XUSB_FALC_CPUCTL			0x100
#define  CPUCTL_STARTCPU			BIT(1)
#define  CPUCTL_STATE_HALTED			BIT(4)
#define  CPUCTL_STATE_STOPPED			BIT(5)
#define XUSB_FALC_BOOTVEC			0x104
#define XUSB_FALC_DMACTL			0x10c
#define XUSB_FALC_IMFILLRNG1			0x154
#define  IMFILLRNG1_TAG_MASK			0xffff
#define  IMFILLRNG1_TAG_LO_SHIFT		0
#define  IMFILLRNG1_TAG_HI_SHIFT		16
#define XUSB_FALC_IMFILLCTL			0x158

/* MP CSB registers */
#define XUSB_CSB_MP_ILOAD_ATTR			0x101a00
#define XUSB_CSB_MP_ILOAD_BASE_LO		0x101a04
#define XUSB_CSB_MP_ILOAD_BASE_HI		0x101a08
#define XUSB_CSB_MP_L2IMEMOP_SIZE		0x101a10
#define  L2IMEMOP_SIZE_SRC_OFFSET_SHIFT		8
#define  L2IMEMOP_SIZE_SRC_OFFSET_MASK		0x3ff
#define  L2IMEMOP_SIZE_SRC_COUNT_SHIFT		24
#define  L2IMEMOP_SIZE_SRC_COUNT_MASK		0xff
#define XUSB_CSB_MP_L2IMEMOP_TRIG		0x101a14
#define  L2IMEMOP_ACTION_SHIFT			24
#define  L2IMEMOP_INVALIDATE_ALL		(0x40 << L2IMEMOP_ACTION_SHIFT)
#define  L2IMEMOP_LOAD_LOCKED_RESULT		(0x11 << L2IMEMOP_ACTION_SHIFT)
#define XUSB_CSB_MEMPOOL_L2IMEMOP_RESULT	0x101a18
#define  L2IMEMOP_RESULT_VLD			BIT(31)
#define XUSB_CSB_MP_APMAP			0x10181c
#define  APMAP_BOOTPATH				BIT(31)

#define IMEM_BLOCK_SIZE				256

struct tegra_xusb_fw_header {
	__le32 boot_loadaddr_in_imem;
	__le32 boot_codedfi_offset;
	__le32 boot_codetag;
	__le32 boot_codesize;
	__le32 phys_memaddr;
	__le16 reqphys_memsize;
	__le16 alloc_phys_memsize;
	__le32 rodata_img_offset;
	__le32 rodata_section_start;
	__le32 rodata_section_end;
	__le32 main_fnaddr;
	__le32 fwimg_cksum;
	__le32 fwimg_created_time;
	__le32 imem_resident_start;
	__le32 imem_resident_end;
	__le32 idirect_start;
	__le32 idirect_end;
	__le32 l2_imem_start;
	__le32 l2_imem_end;
	__le32 version_id;
	u8 init_ddirect;
	u8 reserved[3];
	__le32 phys_addr_log_buffer;
	__le32 total_log_entries;
	__le32 dequeue_ptr;
	__le32 dummy_var[2];
	__le32 fwimg_len;
	u8 magic[8];
	__le32 ss_low_power_entry_timeout;
	u8 num_hsic_port;
	u8 padding[139]; /* Pad to 256 bytes */
};

struct tegra_xusb_phy_type {
	const char *name;
	unsigned int num;
};

struct tegra_xusb_mbox_regs {
	u16 cmd;
	u16 data_in;
	u16 data_out;
	u16 owner;
};

struct tegra_xusb_context_soc {
	struct {
		const unsigned int *offsets;
		unsigned int num_offsets;
	} ipfs;

	struct {
		const unsigned int *offsets;
		unsigned int num_offsets;
	} fpci;
};

struct tegra_xusb_soc {
	const char *firmware;
	const char * const *supply_names;
	unsigned int num_supplies;
	const struct tegra_xusb_phy_type *phy_types;
	unsigned int num_types;
	const struct tegra_xusb_context_soc *context;

	struct {
		struct {
			unsigned int offset;
			unsigned int count;
		} usb2, ulpi, hsic, usb3;
	} ports;

	struct tegra_xusb_mbox_regs mbox;

	bool scale_ss_clock;
	bool has_ipfs;
	bool lpm_support;
	bool otg_reset_sspi;
};

struct tegra_xusb_context {
	u32 *ipfs;
	u32 *fpci;
};

struct tegra_xusb {
	struct device *dev;
	void __iomem *regs;
	struct usb_hcd *hcd;

	struct mutex lock;

	int xhci_irq;
	int mbox_irq;
	int padctl_irq;

	void __iomem *ipfs_base;
	void __iomem *fpci_base;

	const struct tegra_xusb_soc *soc;

	struct regulator_bulk_data *supplies;

	struct tegra_xusb_padctl *padctl;

	struct clk *host_clk;
	struct clk *falcon_clk;
	struct clk *ss_clk;
	struct clk *ss_src_clk;
	struct clk *hs_src_clk;
	struct clk *fs_src_clk;
	struct clk *pll_u_480m;
	struct clk *clk_m;
	struct clk *pll_e;

	struct reset_control *host_rst;
	struct reset_control *ss_rst;

	struct device *genpd_dev_host;
	struct device *genpd_dev_ss;
	bool use_genpd;

	struct phy **phys;
	unsigned int num_phys;

	struct usb_phy **usbphy;
	unsigned int num_usb_phys;
	int otg_usb2_port;
	int otg_usb3_port;
	bool host_mode;
	struct notifier_block id_nb;
	struct work_struct id_work;

	/* Firmware loading related */
	struct {
		size_t size;
		void *virt;
		dma_addr_t phys;
	} fw;

	bool suspended;
	struct tegra_xusb_context context;
};

static struct hc_driver __read_mostly tegra_xhci_hc_driver;

static inline u32 fpci_readl(struct tegra_xusb *tegra, unsigned int offset)
{
	return readl(tegra->fpci_base + offset);
}

static inline void fpci_writel(struct tegra_xusb *tegra, u32 value,
			       unsigned int offset)
{
	writel(value, tegra->fpci_base + offset);
}

static inline u32 ipfs_readl(struct tegra_xusb *tegra, unsigned int offset)
{
	return readl(tegra->ipfs_base + offset);
}

static inline void ipfs_writel(struct tegra_xusb *tegra, u32 value,
			       unsigned int offset)
{
	writel(value, tegra->ipfs_base + offset);
}

static u32 csb_readl(struct tegra_xusb *tegra, unsigned int offset)
{
	u32 page = CSB_PAGE_SELECT(offset);
	u32 ofs = CSB_PAGE_OFFSET(offset);

	fpci_writel(tegra, page, XUSB_CFG_ARU_C11_CSBRANGE);

	return fpci_readl(tegra, XUSB_CFG_CSB_BASE_ADDR + ofs);
}

static void csb_writel(struct tegra_xusb *tegra, u32 value,
		       unsigned int offset)
{
	u32 page = CSB_PAGE_SELECT(offset);
	u32 ofs = CSB_PAGE_OFFSET(offset);

	fpci_writel(tegra, page, XUSB_CFG_ARU_C11_CSBRANGE);
	fpci_writel(tegra, value, XUSB_CFG_CSB_BASE_ADDR + ofs);
}

static int tegra_xusb_set_ss_clk(struct tegra_xusb *tegra,
				 unsigned long rate)
{
	unsigned long new_parent_rate, old_parent_rate;
	struct clk *clk = tegra->ss_src_clk;
	unsigned int div;
	int err;

	if (clk_get_rate(clk) == rate)
		return 0;

	switch (rate) {
	case TEGRA_XHCI_SS_HIGH_SPEED:
		/*
		 * Reparent to PLLU_480M. Set divider first to avoid
		 * overclocking.
		 */
		old_parent_rate = clk_get_rate(clk_get_parent(clk));
		new_parent_rate = clk_get_rate(tegra->pll_u_480m);
		div = new_parent_rate / rate;

		err = clk_set_rate(clk, old_parent_rate / div);
		if (err)
			return err;

		err = clk_set_parent(clk, tegra->pll_u_480m);
		if (err)
			return err;

		/*
		 * The rate should already be correct, but set it again just
		 * to be sure.
		 */
		err = clk_set_rate(clk, rate);
		if (err)
			return err;

		break;

	case TEGRA_XHCI_SS_LOW_SPEED:
		/* Reparent to CLK_M */
		err = clk_set_parent(clk, tegra->clk_m);
		if (err)
			return err;

		err = clk_set_rate(clk, rate);
		if (err)
			return err;

		break;

	default:
		dev_err(tegra->dev, "Invalid SS rate: %lu Hz\n", rate);
		return -EINVAL;
	}

	if (clk_get_rate(clk) != rate) {
		dev_err(tegra->dev, "SS clock doesn't match requested rate\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned long extract_field(u32 value, unsigned int start,
				   unsigned int count)
{
	return (value >> start) & ((1 << count) - 1);
}

/* Command requests from the firmware */
enum tegra_xusb_mbox_cmd {
	MBOX_CMD_MSG_ENABLED = 1,
	MBOX_CMD_INC_FALC_CLOCK,
	MBOX_CMD_DEC_FALC_CLOCK,
	MBOX_CMD_INC_SSPI_CLOCK,
	MBOX_CMD_DEC_SSPI_CLOCK,
	MBOX_CMD_SET_BW, /* no ACK/NAK required */
	MBOX_CMD_SET_SS_PWR_GATING,
	MBOX_CMD_SET_SS_PWR_UNGATING,
	MBOX_CMD_SAVE_DFE_CTLE_CTX,
	MBOX_CMD_AIRPLANE_MODE_ENABLED, /* unused */
	MBOX_CMD_AIRPLANE_MODE_DISABLED, /* unused */
	MBOX_CMD_START_HSIC_IDLE,
	MBOX_CMD_STOP_HSIC_IDLE,
	MBOX_CMD_DBC_WAKE_STACK, /* unused */
	MBOX_CMD_HSIC_PRETEND_CONNECT,
	MBOX_CMD_RESET_SSPI,
	MBOX_CMD_DISABLE_SS_LFPS_DETECTION,
	MBOX_CMD_ENABLE_SS_LFPS_DETECTION,

	MBOX_CMD_MAX,

	/* Response message to above commands */
	MBOX_CMD_ACK = 128,
	MBOX_CMD_NAK
};

struct tegra_xusb_mbox_msg {
	u32 cmd;
	u32 data;
};

static inline u32 tegra_xusb_mbox_pack(const struct tegra_xusb_mbox_msg *msg)
{
	return (msg->cmd & CMD_TYPE_MASK) << CMD_TYPE_SHIFT |
	       (msg->data & CMD_DATA_MASK) << CMD_DATA_SHIFT;
}
static inline void tegra_xusb_mbox_unpack(struct tegra_xusb_mbox_msg *msg,
					  u32 value)
{
	msg->cmd = (value >> CMD_TYPE_SHIFT) & CMD_TYPE_MASK;
	msg->data = (value >> CMD_DATA_SHIFT) & CMD_DATA_MASK;
}

static bool tegra_xusb_mbox_cmd_requires_ack(enum tegra_xusb_mbox_cmd cmd)
{
	switch (cmd) {
	case MBOX_CMD_SET_BW:
	case MBOX_CMD_ACK:
	case MBOX_CMD_NAK:
		return false;

	default:
		return true;
	}
}

static int tegra_xusb_mbox_send(struct tegra_xusb *tegra,
				const struct tegra_xusb_mbox_msg *msg)
{
	bool wait_for_idle = false;
	u32 value;

	/*
	 * Acquire the mailbox. The firmware still owns the mailbox for
	 * ACK/NAK messages.
	 */
	if (!(msg->cmd == MBOX_CMD_ACK || msg->cmd == MBOX_CMD_NAK)) {
		value = fpci_readl(tegra, tegra->soc->mbox.owner);
		if (value != MBOX_OWNER_NONE) {
			dev_err(tegra->dev, "mailbox is busy\n");
			return -EBUSY;
		}

		fpci_writel(tegra, MBOX_OWNER_SW, tegra->soc->mbox.owner);

		value = fpci_readl(tegra, tegra->soc->mbox.owner);
		if (value != MBOX_OWNER_SW) {
			dev_err(tegra->dev, "failed to acquire mailbox\n");
			return -EBUSY;
		}

		wait_for_idle = true;
	}

	value = tegra_xusb_mbox_pack(msg);
	fpci_writel(tegra, value, tegra->soc->mbox.data_in);

	value = fpci_readl(tegra, tegra->soc->mbox.cmd);
	value |= MBOX_INT_EN | MBOX_DEST_FALC;
	fpci_writel(tegra, value, tegra->soc->mbox.cmd);

	if (wait_for_idle) {
		unsigned long timeout = jiffies + msecs_to_jiffies(250);

		while (time_before(jiffies, timeout)) {
			value = fpci_readl(tegra, tegra->soc->mbox.owner);
			if (value == MBOX_OWNER_NONE)
				break;

			usleep_range(10, 20);
		}

		if (time_after(jiffies, timeout))
			value = fpci_readl(tegra, tegra->soc->mbox.owner);

		if (value != MBOX_OWNER_NONE)
			return -ETIMEDOUT;
	}

	return 0;
}

static irqreturn_t tegra_xusb_mbox_irq(int irq, void *data)
{
	struct tegra_xusb *tegra = data;
	u32 value;

	/* clear mailbox interrupts */
	value = fpci_readl(tegra, XUSB_CFG_ARU_SMI_INTR);
	fpci_writel(tegra, value, XUSB_CFG_ARU_SMI_INTR);

	if (value & MBOX_SMI_INTR_FW_HANG)
		dev_err(tegra->dev, "controller firmware hang\n");

	return IRQ_WAKE_THREAD;
}

static void tegra_xusb_mbox_handle(struct tegra_xusb *tegra,
				   const struct tegra_xusb_mbox_msg *msg)
{
	struct tegra_xusb_padctl *padctl = tegra->padctl;
	const struct tegra_xusb_soc *soc = tegra->soc;
	struct device *dev = tegra->dev;
	struct tegra_xusb_mbox_msg rsp;
	unsigned long mask;
	unsigned int port;
	bool idle, enable;
	int err = 0;

	memset(&rsp, 0, sizeof(rsp));

	switch (msg->cmd) {
	case MBOX_CMD_INC_FALC_CLOCK:
	case MBOX_CMD_DEC_FALC_CLOCK:
		rsp.data = clk_get_rate(tegra->falcon_clk) / 1000;
		if (rsp.data != msg->data)
			rsp.cmd = MBOX_CMD_NAK;
		else
			rsp.cmd = MBOX_CMD_ACK;

		break;

	case MBOX_CMD_INC_SSPI_CLOCK:
	case MBOX_CMD_DEC_SSPI_CLOCK:
		if (tegra->soc->scale_ss_clock) {
			err = tegra_xusb_set_ss_clk(tegra, msg->data * 1000);
			if (err < 0)
				rsp.cmd = MBOX_CMD_NAK;
			else
				rsp.cmd = MBOX_CMD_ACK;

			rsp.data = clk_get_rate(tegra->ss_src_clk) / 1000;
		} else {
			rsp.cmd = MBOX_CMD_ACK;
			rsp.data = msg->data;
		}

		break;

	case MBOX_CMD_SET_BW:
		/*
		 * TODO: Request bandwidth once EMC scaling is supported.
		 * Ignore for now since ACK/NAK is not required for SET_BW
		 * messages.
		 */
		break;

	case MBOX_CMD_SAVE_DFE_CTLE_CTX:
		err = tegra_xusb_padctl_usb3_save_context(padctl, msg->data);
		if (err < 0) {
			dev_err(dev, "failed to save context for USB3#%u: %d\n",
				msg->data, err);
			rsp.cmd = MBOX_CMD_NAK;
		} else {
			rsp.cmd = MBOX_CMD_ACK;
		}

		rsp.data = msg->data;
		break;

	case MBOX_CMD_START_HSIC_IDLE:
	case MBOX_CMD_STOP_HSIC_IDLE:
		if (msg->cmd == MBOX_CMD_STOP_HSIC_IDLE)
			idle = false;
		else
			idle = true;

		mask = extract_field(msg->data, 1 + soc->ports.hsic.offset,
				     soc->ports.hsic.count);

		for_each_set_bit(port, &mask, 32) {
			err = tegra_xusb_padctl_hsic_set_idle(padctl, port,
							      idle);
			if (err < 0)
				break;
		}

		if (err < 0) {
			dev_err(dev, "failed to set HSIC#%u %s: %d\n", port,
				idle ? "idle" : "busy", err);
			rsp.cmd = MBOX_CMD_NAK;
		} else {
			rsp.cmd = MBOX_CMD_ACK;
		}

		rsp.data = msg->data;
		break;

	case MBOX_CMD_DISABLE_SS_LFPS_DETECTION:
	case MBOX_CMD_ENABLE_SS_LFPS_DETECTION:
		if (msg->cmd == MBOX_CMD_DISABLE_SS_LFPS_DETECTION)
			enable = false;
		else
			enable = true;

		mask = extract_field(msg->data, 1 + soc->ports.usb3.offset,
				     soc->ports.usb3.count);

		for_each_set_bit(port, &mask, soc->ports.usb3.count) {
			err = tegra_xusb_padctl_usb3_set_lfps_detect(padctl,
								     port,
								     enable);
			if (err < 0)
				break;

			/*
			 * wait 500us for LFPS detector to be disabled before
			 * sending ACK
			 */
			if (!enable)
				usleep_range(500, 1000);
		}

		if (err < 0) {
			dev_err(dev,
				"failed to %s LFPS detection on USB3#%u: %d\n",
				enable ? "enable" : "disable", port, err);
			rsp.cmd = MBOX_CMD_NAK;
		} else {
			rsp.cmd = MBOX_CMD_ACK;
		}

		rsp.data = msg->data;
		break;

	default:
		dev_warn(dev, "unknown message: %#x\n", msg->cmd);
		break;
	}

	if (rsp.cmd) {
		const char *cmd = (rsp.cmd == MBOX_CMD_ACK) ? "ACK" : "NAK";

		err = tegra_xusb_mbox_send(tegra, &rsp);
		if (err < 0)
			dev_err(dev, "failed to send %s: %d\n", cmd, err);
	}
}

static irqreturn_t tegra_xusb_mbox_thread(int irq, void *data)
{
	struct tegra_xusb *tegra = data;
	struct tegra_xusb_mbox_msg msg;
	u32 value;

	mutex_lock(&tegra->lock);

	if (pm_runtime_suspended(tegra->dev) || tegra->suspended)
		goto out;

	value = fpci_readl(tegra, tegra->soc->mbox.data_out);
	tegra_xusb_mbox_unpack(&msg, value);

	value = fpci_readl(tegra, tegra->soc->mbox.cmd);
	value &= ~MBOX_DEST_SMI;
	fpci_writel(tegra, value, tegra->soc->mbox.cmd);

	/* clear mailbox owner if no ACK/NAK is required */
	if (!tegra_xusb_mbox_cmd_requires_ack(msg.cmd))
		fpci_writel(tegra, MBOX_OWNER_NONE, tegra->soc->mbox.owner);

	tegra_xusb_mbox_handle(tegra, &msg);

out:
	mutex_unlock(&tegra->lock);
	return IRQ_HANDLED;
}

static void tegra_xusb_config(struct tegra_xusb *tegra)
{
	u32 regs = tegra->hcd->rsrc_start;
	u32 value;

	if (tegra->soc->has_ipfs) {
		value = ipfs_readl(tegra, IPFS_XUSB_HOST_CONFIGURATION_0);
		value |= IPFS_EN_FPCI;
		ipfs_writel(tegra, value, IPFS_XUSB_HOST_CONFIGURATION_0);

		usleep_range(10, 20);
	}

	/* Program BAR0 space */
	value = fpci_readl(tegra, XUSB_CFG_4);
	value &= ~(XUSB_BASE_ADDR_MASK << XUSB_BASE_ADDR_SHIFT);
	value |= regs & (XUSB_BASE_ADDR_MASK << XUSB_BASE_ADDR_SHIFT);
	fpci_writel(tegra, value, XUSB_CFG_4);

	usleep_range(100, 200);

	/* Enable bus master */
	value = fpci_readl(tegra, XUSB_CFG_1);
	value |= XUSB_IO_SPACE_EN | XUSB_MEM_SPACE_EN | XUSB_BUS_MASTER_EN;
	fpci_writel(tegra, value, XUSB_CFG_1);

	if (tegra->soc->has_ipfs) {
		/* Enable interrupt assertion */
		value = ipfs_readl(tegra, IPFS_XUSB_HOST_INTR_MASK_0);
		value |= IPFS_IP_INT_MASK;
		ipfs_writel(tegra, value, IPFS_XUSB_HOST_INTR_MASK_0);

		/* Set hysteresis */
		ipfs_writel(tegra, 0x80, IPFS_XUSB_HOST_CLKGATE_HYSTERESIS_0);
	}
}

static int tegra_xusb_clk_enable(struct tegra_xusb *tegra)
{
	int err;

	err = clk_prepare_enable(tegra->pll_e);
	if (err < 0)
		return err;

	err = clk_prepare_enable(tegra->host_clk);
	if (err < 0)
		goto disable_plle;

	err = clk_prepare_enable(tegra->ss_clk);
	if (err < 0)
		goto disable_host;

	err = clk_prepare_enable(tegra->falcon_clk);
	if (err < 0)
		goto disable_ss;

	err = clk_prepare_enable(tegra->fs_src_clk);
	if (err < 0)
		goto disable_falc;

	err = clk_prepare_enable(tegra->hs_src_clk);
	if (err < 0)
		goto disable_fs_src;

	if (tegra->soc->scale_ss_clock) {
		err = tegra_xusb_set_ss_clk(tegra, TEGRA_XHCI_SS_HIGH_SPEED);
		if (err < 0)
			goto disable_hs_src;
	}

	return 0;

disable_hs_src:
	clk_disable_unprepare(tegra->hs_src_clk);
disable_fs_src:
	clk_disable_unprepare(tegra->fs_src_clk);
disable_falc:
	clk_disable_unprepare(tegra->falcon_clk);
disable_ss:
	clk_disable_unprepare(tegra->ss_clk);
disable_host:
	clk_disable_unprepare(tegra->host_clk);
disable_plle:
	clk_disable_unprepare(tegra->pll_e);
	return err;
}

static void tegra_xusb_clk_disable(struct tegra_xusb *tegra)
{
	clk_disable_unprepare(tegra->pll_e);
	clk_disable_unprepare(tegra->host_clk);
	clk_disable_unprepare(tegra->ss_clk);
	clk_disable_unprepare(tegra->falcon_clk);
	clk_disable_unprepare(tegra->fs_src_clk);
	clk_disable_unprepare(tegra->hs_src_clk);
}

static int tegra_xusb_phy_enable(struct tegra_xusb *tegra)
{
	unsigned int i;
	int err;

	for (i = 0; i < tegra->num_phys; i++) {
		err = phy_init(tegra->phys[i]);
		if (err)
			goto disable_phy;

		err = phy_power_on(tegra->phys[i]);
		if (err) {
			phy_exit(tegra->phys[i]);
			goto disable_phy;
		}
	}

	return 0;

disable_phy:
	while (i--) {
		phy_power_off(tegra->phys[i]);
		phy_exit(tegra->phys[i]);
	}

	return err;
}

static void tegra_xusb_phy_disable(struct tegra_xusb *tegra)
{
	unsigned int i;

	for (i = 0; i < tegra->num_phys; i++) {
		phy_power_off(tegra->phys[i]);
		phy_exit(tegra->phys[i]);
	}
}

#ifdef CONFIG_PM_SLEEP
static int tegra_xusb_init_context(struct tegra_xusb *tegra)
{
	const struct tegra_xusb_context_soc *soc = tegra->soc->context;

	tegra->context.ipfs = devm_kcalloc(tegra->dev, soc->ipfs.num_offsets,
					   sizeof(u32), GFP_KERNEL);
	if (!tegra->context.ipfs)
		return -ENOMEM;

	tegra->context.fpci = devm_kcalloc(tegra->dev, soc->fpci.num_offsets,
					   sizeof(u32), GFP_KERNEL);
	if (!tegra->context.fpci)
		return -ENOMEM;

	return 0;
}
#else
static inline int tegra_xusb_init_context(struct tegra_xusb *tegra)
{
	return 0;
}
#endif

static int tegra_xusb_request_firmware(struct tegra_xusb *tegra)
{
	struct tegra_xusb_fw_header *header;
	const struct firmware *fw;
	int err;

	err = request_firmware(&fw, tegra->soc->firmware, tegra->dev);
	if (err < 0) {
		dev_err(tegra->dev, "failed to request firmware: %d\n", err);
		return err;
	}

	/* Load Falcon controller with its firmware. */
	header = (struct tegra_xusb_fw_header *)fw->data;
	tegra->fw.size = le32_to_cpu(header->fwimg_len);

	tegra->fw.virt = dma_alloc_coherent(tegra->dev, tegra->fw.size,
					    &tegra->fw.phys, GFP_KERNEL);
	if (!tegra->fw.virt) {
		dev_err(tegra->dev, "failed to allocate memory for firmware\n");
		release_firmware(fw);
		return -ENOMEM;
	}

	header = (struct tegra_xusb_fw_header *)tegra->fw.virt;
	memcpy(tegra->fw.virt, fw->data, tegra->fw.size);
	release_firmware(fw);

	return 0;
}

static int tegra_xusb_load_firmware(struct tegra_xusb *tegra)
{
	unsigned int code_tag_blocks, code_size_blocks, code_blocks;
	struct xhci_cap_regs __iomem *cap = tegra->regs;
	struct tegra_xusb_fw_header *header;
	struct device *dev = tegra->dev;
	struct xhci_op_regs __iomem *op;
	unsigned long timeout;
	time64_t timestamp;
	u64 address;
	u32 value;
	int err;

	header = (struct tegra_xusb_fw_header *)tegra->fw.virt;
	op = tegra->regs + HC_LENGTH(readl(&cap->hc_capbase));

	if (csb_readl(tegra, XUSB_CSB_MP_ILOAD_BASE_LO) != 0) {
		dev_info(dev, "Firmware already loaded, Falcon state %#x\n",
			 csb_readl(tegra, XUSB_FALC_CPUCTL));
		return 0;
	}

	/* Program the size of DFI into ILOAD_ATTR. */
	csb_writel(tegra, tegra->fw.size, XUSB_CSB_MP_ILOAD_ATTR);

	/*
	 * Boot code of the firmware reads the ILOAD_BASE registers
	 * to get to the start of the DFI in system memory.
	 */
	address = tegra->fw.phys + sizeof(*header);
	csb_writel(tegra, address >> 32, XUSB_CSB_MP_ILOAD_BASE_HI);
	csb_writel(tegra, address, XUSB_CSB_MP_ILOAD_BASE_LO);

	/* Set BOOTPATH to 1 in APMAP. */
	csb_writel(tegra, APMAP_BOOTPATH, XUSB_CSB_MP_APMAP);

	/* Invalidate L2IMEM. */
	csb_writel(tegra, L2IMEMOP_INVALIDATE_ALL, XUSB_CSB_MP_L2IMEMOP_TRIG);

	/*
	 * Initiate fetch of bootcode from system memory into L2IMEM.
	 * Program bootcode location and size in system memory.
	 */
	code_tag_blocks = DIV_ROUND_UP(le32_to_cpu(header->boot_codetag),
				       IMEM_BLOCK_SIZE);
	code_size_blocks = DIV_ROUND_UP(le32_to_cpu(header->boot_codesize),
					IMEM_BLOCK_SIZE);
	code_blocks = code_tag_blocks + code_size_blocks;

	value = ((code_tag_blocks & L2IMEMOP_SIZE_SRC_OFFSET_MASK) <<
			L2IMEMOP_SIZE_SRC_OFFSET_SHIFT) |
		((code_size_blocks & L2IMEMOP_SIZE_SRC_COUNT_MASK) <<
			L2IMEMOP_SIZE_SRC_COUNT_SHIFT);
	csb_writel(tegra, value, XUSB_CSB_MP_L2IMEMOP_SIZE);

	/* Trigger L2IMEM load operation. */
	csb_writel(tegra, L2IMEMOP_LOAD_LOCKED_RESULT,
		   XUSB_CSB_MP_L2IMEMOP_TRIG);

	/* Setup Falcon auto-fill. */
	csb_writel(tegra, code_size_blocks, XUSB_FALC_IMFILLCTL);

	value = ((code_tag_blocks & IMFILLRNG1_TAG_MASK) <<
			IMFILLRNG1_TAG_LO_SHIFT) |
		((code_blocks & IMFILLRNG1_TAG_MASK) <<
			IMFILLRNG1_TAG_HI_SHIFT);
	csb_writel(tegra, value, XUSB_FALC_IMFILLRNG1);

	csb_writel(tegra, 0, XUSB_FALC_DMACTL);

	/* wait for RESULT_VLD to get set */
#define tegra_csb_readl(offset) csb_readl(tegra, offset)
	err = readx_poll_timeout(tegra_csb_readl,
				 XUSB_CSB_MEMPOOL_L2IMEMOP_RESULT, value,
				 value & L2IMEMOP_RESULT_VLD, 100, 10000);
	if (err < 0) {
		dev_err(dev, "DMA controller not ready %#010x\n", value);
		return err;
	}
#undef tegra_csb_readl

	csb_writel(tegra, le32_to_cpu(header->boot_codetag),
		   XUSB_FALC_BOOTVEC);

	/* Boot Falcon CPU and wait for USBSTS_CNR to get cleared. */
	csb_writel(tegra, CPUCTL_STARTCPU, XUSB_FALC_CPUCTL);

	timeout = jiffies + msecs_to_jiffies(200);

	do {
		value = readl(&op->status);
		if ((value & STS_CNR) == 0)
			break;

		usleep_range(1000, 2000);
	} while (time_is_after_jiffies(timeout));

	value = readl(&op->status);
	if (value & STS_CNR) {
		value = csb_readl(tegra, XUSB_FALC_CPUCTL);
		dev_err(dev, "XHCI controller not read: %#010x\n", value);
		return -EIO;
	}

	timestamp = le32_to_cpu(header->fwimg_created_time);

	dev_info(dev, "Firmware timestamp: %ptTs UTC\n", &timestamp);

	return 0;
}

static void tegra_xusb_powerdomain_remove(struct device *dev,
					  struct tegra_xusb *tegra)
{
	if (!tegra->use_genpd)
		return;

	if (!IS_ERR_OR_NULL(tegra->genpd_dev_ss))
		dev_pm_domain_detach(tegra->genpd_dev_ss, true);
	if (!IS_ERR_OR_NULL(tegra->genpd_dev_host))
		dev_pm_domain_detach(tegra->genpd_dev_host, true);
}

static int tegra_xusb_powerdomain_init(struct device *dev,
				       struct tegra_xusb *tegra)
{
	int err;

	tegra->genpd_dev_host = dev_pm_domain_attach_by_name(dev, "xusb_host");
	if (IS_ERR(tegra->genpd_dev_host)) {
		err = PTR_ERR(tegra->genpd_dev_host);
		dev_err(dev, "failed to get host pm-domain: %d\n", err);
		return err;
	}

	tegra->genpd_dev_ss = dev_pm_domain_attach_by_name(dev, "xusb_ss");
	if (IS_ERR(tegra->genpd_dev_ss)) {
		err = PTR_ERR(tegra->genpd_dev_ss);
		dev_err(dev, "failed to get superspeed pm-domain: %d\n", err);
		return err;
	}

	tegra->use_genpd = true;

	return 0;
}

static int tegra_xusb_unpowergate_partitions(struct tegra_xusb *tegra)
{
	struct device *dev = tegra->dev;
	int rc;

	if (tegra->use_genpd) {
		rc = pm_runtime_resume_and_get(tegra->genpd_dev_ss);
		if (rc < 0) {
			dev_err(dev, "failed to enable XUSB SS partition\n");
			return rc;
		}

		rc = pm_runtime_resume_and_get(tegra->genpd_dev_host);
		if (rc < 0) {
			dev_err(dev, "failed to enable XUSB Host partition\n");
			pm_runtime_put_sync(tegra->genpd_dev_ss);
			return rc;
		}
	} else {
		rc = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_XUSBA,
							tegra->ss_clk,
							tegra->ss_rst);
		if (rc < 0) {
			dev_err(dev, "failed to enable XUSB SS partition\n");
			return rc;
		}

		rc = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_XUSBC,
							tegra->host_clk,
							tegra->host_rst);
		if (rc < 0) {
			dev_err(dev, "failed to enable XUSB Host partition\n");
			tegra_powergate_power_off(TEGRA_POWERGATE_XUSBA);
			return rc;
		}
	}

	return 0;
}

static int tegra_xusb_powergate_partitions(struct tegra_xusb *tegra)
{
	struct device *dev = tegra->dev;
	int rc;

	if (tegra->use_genpd) {
		rc = pm_runtime_put_sync(tegra->genpd_dev_host);
		if (rc < 0) {
			dev_err(dev, "failed to disable XUSB Host partition\n");
			return rc;
		}

		rc = pm_runtime_put_sync(tegra->genpd_dev_ss);
		if (rc < 0) {
			dev_err(dev, "failed to disable XUSB SS partition\n");
			pm_runtime_get_sync(tegra->genpd_dev_host);
			return rc;
		}
	} else {
		rc = tegra_powergate_power_off(TEGRA_POWERGATE_XUSBC);
		if (rc < 0) {
			dev_err(dev, "failed to disable XUSB Host partition\n");
			return rc;
		}

		rc = tegra_powergate_power_off(TEGRA_POWERGATE_XUSBA);
		if (rc < 0) {
			dev_err(dev, "failed to disable XUSB SS partition\n");
			tegra_powergate_sequence_power_up(TEGRA_POWERGATE_XUSBC,
							  tegra->host_clk,
							  tegra->host_rst);
			return rc;
		}
	}

	return 0;
}

static int __tegra_xusb_enable_firmware_messages(struct tegra_xusb *tegra)
{
	struct tegra_xusb_mbox_msg msg;
	int err;

	/* Enable firmware messages from controller. */
	msg.cmd = MBOX_CMD_MSG_ENABLED;
	msg.data = 0;

	err = tegra_xusb_mbox_send(tegra, &msg);
	if (err < 0)
		dev_err(tegra->dev, "failed to enable messages: %d\n", err);

	return err;
}

static irqreturn_t tegra_xusb_padctl_irq(int irq, void *data)
{
	struct tegra_xusb *tegra = data;

	mutex_lock(&tegra->lock);

	if (tegra->suspended) {
		mutex_unlock(&tegra->lock);
		return IRQ_HANDLED;
	}

	mutex_unlock(&tegra->lock);

	pm_runtime_resume(tegra->dev);

	return IRQ_HANDLED;
}

static int tegra_xusb_enable_firmware_messages(struct tegra_xusb *tegra)
{
	int err;

	mutex_lock(&tegra->lock);
	err = __tegra_xusb_enable_firmware_messages(tegra);
	mutex_unlock(&tegra->lock);

	return err;
}

static void tegra_xhci_set_port_power(struct tegra_xusb *tegra, bool main,
						 bool set)
{
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);
	struct usb_hcd *hcd = main ?  xhci->main_hcd : xhci->shared_hcd;
	unsigned int wait = (!main && !set) ? 1000 : 10;
	u16 typeReq = set ? SetPortFeature : ClearPortFeature;
	u16 wIndex = main ? tegra->otg_usb2_port + 1 : tegra->otg_usb3_port + 1;
	u32 status;
	u32 stat_power = main ? USB_PORT_STAT_POWER : USB_SS_PORT_STAT_POWER;
	u32 status_val = set ? stat_power : 0;

	dev_dbg(tegra->dev, "%s():%s %s port power\n", __func__,
		set ? "set" : "clear", main ? "HS" : "SS");

	hcd->driver->hub_control(hcd, typeReq, USB_PORT_FEAT_POWER, wIndex,
				 NULL, 0);

	do {
		tegra_xhci_hc_driver.hub_control(hcd, GetPortStatus, 0, wIndex,
					(char *) &status, sizeof(status));
		if (status_val == (status & stat_power))
			break;

		if (!main && !set)
			usleep_range(600, 700);
		else
			usleep_range(10, 20);
	} while (--wait > 0);

	if (status_val != (status & stat_power))
		dev_info(tegra->dev, "failed to %s %s PP %d\n",
						set ? "set" : "clear",
						main ? "HS" : "SS", status);
}

static struct phy *tegra_xusb_get_phy(struct tegra_xusb *tegra, char *name,
								int port)
{
	unsigned int i, phy_count = 0;

	for (i = 0; i < tegra->soc->num_types; i++) {
		if (!strncmp(tegra->soc->phy_types[i].name, name,
							    strlen(name)))
			return tegra->phys[phy_count+port];

		phy_count += tegra->soc->phy_types[i].num;
	}

	return NULL;
}

static void tegra_xhci_id_work(struct work_struct *work)
{
	struct tegra_xusb *tegra = container_of(work, struct tegra_xusb,
						id_work);
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);
	struct tegra_xusb_mbox_msg msg;
	struct phy *phy = tegra_xusb_get_phy(tegra, "usb2",
						    tegra->otg_usb2_port);
	u32 status;
	int ret;

	dev_dbg(tegra->dev, "host mode %s\n", tegra->host_mode ? "on" : "off");

	mutex_lock(&tegra->lock);

	if (tegra->host_mode)
		phy_set_mode_ext(phy, PHY_MODE_USB_OTG, USB_ROLE_HOST);
	else
		phy_set_mode_ext(phy, PHY_MODE_USB_OTG, USB_ROLE_NONE);

	mutex_unlock(&tegra->lock);

	tegra->otg_usb3_port = tegra_xusb_padctl_get_usb3_companion(tegra->padctl,
								    tegra->otg_usb2_port);

	if (tegra->host_mode) {
		/* switch to host mode */
		if (tegra->otg_usb3_port >= 0) {
			if (tegra->soc->otg_reset_sspi) {
				/* set PP=0 */
				tegra_xhci_hc_driver.hub_control(
					xhci->shared_hcd, GetPortStatus,
					0, tegra->otg_usb3_port+1,
					(char *) &status, sizeof(status));
				if (status & USB_SS_PORT_STAT_POWER)
					tegra_xhci_set_port_power(tegra, false,
								  false);

				/* reset OTG port SSPI */
				msg.cmd = MBOX_CMD_RESET_SSPI;
				msg.data = tegra->otg_usb3_port+1;

				ret = tegra_xusb_mbox_send(tegra, &msg);
				if (ret < 0) {
					dev_info(tegra->dev,
						"failed to RESET_SSPI %d\n",
						ret);
				}
			}

			tegra_xhci_set_port_power(tegra, false, true);
		}

		tegra_xhci_set_port_power(tegra, true, true);

	} else {
		if (tegra->otg_usb3_port >= 0)
			tegra_xhci_set_port_power(tegra, false, false);

		tegra_xhci_set_port_power(tegra, true, false);
	}
}

#if IS_ENABLED(CONFIG_PM) || IS_ENABLED(CONFIG_PM_SLEEP)
static bool is_usb2_otg_phy(struct tegra_xusb *tegra, unsigned int index)
{
	return (tegra->usbphy[index] != NULL);
}

static bool is_usb3_otg_phy(struct tegra_xusb *tegra, unsigned int index)
{
	struct tegra_xusb_padctl *padctl = tegra->padctl;
	unsigned int i;
	int port;

	for (i = 0; i < tegra->num_usb_phys; i++) {
		if (is_usb2_otg_phy(tegra, i)) {
			port = tegra_xusb_padctl_get_usb3_companion(padctl, i);
			if ((port >= 0) && (index == (unsigned int)port))
				return true;
		}
	}

	return false;
}

static bool is_host_mode_phy(struct tegra_xusb *tegra, unsigned int phy_type, unsigned int index)
{
	if (strcmp(tegra->soc->phy_types[phy_type].name, "hsic") == 0)
		return true;

	if (strcmp(tegra->soc->phy_types[phy_type].name, "usb2") == 0) {
		if (is_usb2_otg_phy(tegra, index))
			return ((index == tegra->otg_usb2_port) && tegra->host_mode);
		else
			return true;
	}

	if (strcmp(tegra->soc->phy_types[phy_type].name, "usb3") == 0) {
		if (is_usb3_otg_phy(tegra, index))
			return ((index == tegra->otg_usb3_port) && tegra->host_mode);
		else
			return true;
	}

	return false;
}
#endif

static int tegra_xusb_get_usb2_port(struct tegra_xusb *tegra,
					      struct usb_phy *usbphy)
{
	unsigned int i;

	for (i = 0; i < tegra->num_usb_phys; i++) {
		if (tegra->usbphy[i] && usbphy == tegra->usbphy[i])
			return i;
	}

	return -1;
}

static int tegra_xhci_id_notify(struct notifier_block *nb,
					 unsigned long action, void *data)
{
	struct tegra_xusb *tegra = container_of(nb, struct tegra_xusb,
						    id_nb);
	struct usb_phy *usbphy = (struct usb_phy *)data;

	dev_dbg(tegra->dev, "%s(): action is %d", __func__, usbphy->last_event);

	if ((tegra->host_mode && usbphy->last_event == USB_EVENT_ID) ||
		(!tegra->host_mode && usbphy->last_event != USB_EVENT_ID)) {
		dev_dbg(tegra->dev, "Same role(%d) received. Ignore",
			tegra->host_mode);
		return NOTIFY_OK;
	}

	tegra->otg_usb2_port = tegra_xusb_get_usb2_port(tegra, usbphy);

	tegra->host_mode = (usbphy->last_event == USB_EVENT_ID) ? true : false;

	schedule_work(&tegra->id_work);

	return NOTIFY_OK;
}

static int tegra_xusb_init_usb_phy(struct tegra_xusb *tegra)
{
	unsigned int i;

	tegra->usbphy = devm_kcalloc(tegra->dev, tegra->num_usb_phys,
				   sizeof(*tegra->usbphy), GFP_KERNEL);
	if (!tegra->usbphy)
		return -ENOMEM;

	INIT_WORK(&tegra->id_work, tegra_xhci_id_work);
	tegra->id_nb.notifier_call = tegra_xhci_id_notify;
	tegra->otg_usb2_port = -EINVAL;
	tegra->otg_usb3_port = -EINVAL;

	for (i = 0; i < tegra->num_usb_phys; i++) {
		struct phy *phy = tegra_xusb_get_phy(tegra, "usb2", i);

		if (!phy)
			continue;

		tegra->usbphy[i] = devm_usb_get_phy_by_node(tegra->dev,
							phy->dev.of_node,
							&tegra->id_nb);
		if (!IS_ERR(tegra->usbphy[i])) {
			dev_dbg(tegra->dev, "usbphy-%d registered", i);
			otg_set_host(tegra->usbphy[i]->otg, &tegra->hcd->self);
		} else {
			/*
			 * usb-phy is optional, continue if its not available.
			 */
			tegra->usbphy[i] = NULL;
		}
	}

	return 0;
}

static void tegra_xusb_deinit_usb_phy(struct tegra_xusb *tegra)
{
	unsigned int i;

	cancel_work_sync(&tegra->id_work);

	for (i = 0; i < tegra->num_usb_phys; i++)
		if (tegra->usbphy[i])
			otg_set_host(tegra->usbphy[i]->otg, NULL);
}

static int tegra_xusb_probe(struct platform_device *pdev)
{
	struct of_phandle_args args;
	struct tegra_xusb *tegra;
	struct device_node *np;
	struct resource *regs;
	struct xhci_hcd *xhci;
	unsigned int i, j, k;
	struct phy *phy;
	int err;

	BUILD_BUG_ON(sizeof(struct tegra_xusb_fw_header) != 256);

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	tegra->soc = of_device_get_match_data(&pdev->dev);
	mutex_init(&tegra->lock);
	tegra->dev = &pdev->dev;

	err = tegra_xusb_init_context(tegra);
	if (err < 0)
		return err;

	tegra->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &regs);
	if (IS_ERR(tegra->regs))
		return PTR_ERR(tegra->regs);

	tegra->fpci_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(tegra->fpci_base))
		return PTR_ERR(tegra->fpci_base);

	if (tegra->soc->has_ipfs) {
		tegra->ipfs_base = devm_platform_ioremap_resource(pdev, 2);
		if (IS_ERR(tegra->ipfs_base))
			return PTR_ERR(tegra->ipfs_base);
	}

	tegra->xhci_irq = platform_get_irq(pdev, 0);
	if (tegra->xhci_irq < 0)
		return tegra->xhci_irq;

	tegra->mbox_irq = platform_get_irq(pdev, 1);
	if (tegra->mbox_irq < 0)
		return tegra->mbox_irq;

	tegra->padctl = tegra_xusb_padctl_get(&pdev->dev);
	if (IS_ERR(tegra->padctl))
		return PTR_ERR(tegra->padctl);

	np = of_parse_phandle(pdev->dev.of_node, "nvidia,xusb-padctl", 0);
	if (!np) {
		err = -ENODEV;
		goto put_padctl;
	}

	/* Older device-trees don't have padctrl interrupt */
	err = of_irq_parse_one(np, 0, &args);
	if (!err) {
		tegra->padctl_irq = of_irq_get(np, 0);
		if (tegra->padctl_irq <= 0) {
			err = (tegra->padctl_irq == 0) ? -ENODEV : tegra->padctl_irq;
			goto put_padctl;
		}
	} else {
		dev_dbg(&pdev->dev,
			"%pOF is missing an interrupt, disabling PM support\n", np);
	}

	tegra->host_clk = devm_clk_get(&pdev->dev, "xusb_host");
	if (IS_ERR(tegra->host_clk)) {
		err = PTR_ERR(tegra->host_clk);
		dev_err(&pdev->dev, "failed to get xusb_host: %d\n", err);
		goto put_padctl;
	}

	tegra->falcon_clk = devm_clk_get(&pdev->dev, "xusb_falcon_src");
	if (IS_ERR(tegra->falcon_clk)) {
		err = PTR_ERR(tegra->falcon_clk);
		dev_err(&pdev->dev, "failed to get xusb_falcon_src: %d\n", err);
		goto put_padctl;
	}

	tegra->ss_clk = devm_clk_get(&pdev->dev, "xusb_ss");
	if (IS_ERR(tegra->ss_clk)) {
		err = PTR_ERR(tegra->ss_clk);
		dev_err(&pdev->dev, "failed to get xusb_ss: %d\n", err);
		goto put_padctl;
	}

	tegra->ss_src_clk = devm_clk_get(&pdev->dev, "xusb_ss_src");
	if (IS_ERR(tegra->ss_src_clk)) {
		err = PTR_ERR(tegra->ss_src_clk);
		dev_err(&pdev->dev, "failed to get xusb_ss_src: %d\n", err);
		goto put_padctl;
	}

	tegra->hs_src_clk = devm_clk_get(&pdev->dev, "xusb_hs_src");
	if (IS_ERR(tegra->hs_src_clk)) {
		err = PTR_ERR(tegra->hs_src_clk);
		dev_err(&pdev->dev, "failed to get xusb_hs_src: %d\n", err);
		goto put_padctl;
	}

	tegra->fs_src_clk = devm_clk_get(&pdev->dev, "xusb_fs_src");
	if (IS_ERR(tegra->fs_src_clk)) {
		err = PTR_ERR(tegra->fs_src_clk);
		dev_err(&pdev->dev, "failed to get xusb_fs_src: %d\n", err);
		goto put_padctl;
	}

	tegra->pll_u_480m = devm_clk_get(&pdev->dev, "pll_u_480m");
	if (IS_ERR(tegra->pll_u_480m)) {
		err = PTR_ERR(tegra->pll_u_480m);
		dev_err(&pdev->dev, "failed to get pll_u_480m: %d\n", err);
		goto put_padctl;
	}

	tegra->clk_m = devm_clk_get(&pdev->dev, "clk_m");
	if (IS_ERR(tegra->clk_m)) {
		err = PTR_ERR(tegra->clk_m);
		dev_err(&pdev->dev, "failed to get clk_m: %d\n", err);
		goto put_padctl;
	}

	tegra->pll_e = devm_clk_get(&pdev->dev, "pll_e");
	if (IS_ERR(tegra->pll_e)) {
		err = PTR_ERR(tegra->pll_e);
		dev_err(&pdev->dev, "failed to get pll_e: %d\n", err);
		goto put_padctl;
	}

	if (!of_property_read_bool(pdev->dev.of_node, "power-domains")) {
		tegra->host_rst = devm_reset_control_get(&pdev->dev,
							 "xusb_host");
		if (IS_ERR(tegra->host_rst)) {
			err = PTR_ERR(tegra->host_rst);
			dev_err(&pdev->dev,
				"failed to get xusb_host reset: %d\n", err);
			goto put_padctl;
		}

		tegra->ss_rst = devm_reset_control_get(&pdev->dev, "xusb_ss");
		if (IS_ERR(tegra->ss_rst)) {
			err = PTR_ERR(tegra->ss_rst);
			dev_err(&pdev->dev, "failed to get xusb_ss reset: %d\n",
				err);
			goto put_padctl;
		}
	} else {
		err = tegra_xusb_powerdomain_init(&pdev->dev, tegra);
		if (err)
			goto put_powerdomains;
	}

	tegra->supplies = devm_kcalloc(&pdev->dev, tegra->soc->num_supplies,
				       sizeof(*tegra->supplies), GFP_KERNEL);
	if (!tegra->supplies) {
		err = -ENOMEM;
		goto put_powerdomains;
	}

	regulator_bulk_set_supply_names(tegra->supplies,
					tegra->soc->supply_names,
					tegra->soc->num_supplies);

	err = devm_regulator_bulk_get(&pdev->dev, tegra->soc->num_supplies,
				      tegra->supplies);
	if (err) {
		dev_err(&pdev->dev, "failed to get regulators: %d\n", err);
		goto put_powerdomains;
	}

	for (i = 0; i < tegra->soc->num_types; i++) {
		if (!strncmp(tegra->soc->phy_types[i].name, "usb2", 4))
			tegra->num_usb_phys = tegra->soc->phy_types[i].num;
		tegra->num_phys += tegra->soc->phy_types[i].num;
	}

	tegra->phys = devm_kcalloc(&pdev->dev, tegra->num_phys,
				   sizeof(*tegra->phys), GFP_KERNEL);
	if (!tegra->phys) {
		err = -ENOMEM;
		goto put_powerdomains;
	}

	for (i = 0, k = 0; i < tegra->soc->num_types; i++) {
		char prop[8];

		for (j = 0; j < tegra->soc->phy_types[i].num; j++) {
			snprintf(prop, sizeof(prop), "%s-%d",
				 tegra->soc->phy_types[i].name, j);

			phy = devm_phy_optional_get(&pdev->dev, prop);
			if (IS_ERR(phy)) {
				dev_err(&pdev->dev,
					"failed to get PHY %s: %ld\n", prop,
					PTR_ERR(phy));
				err = PTR_ERR(phy);
				goto put_powerdomains;
			}

			tegra->phys[k++] = phy;
		}
	}

	tegra->hcd = usb_create_hcd(&tegra_xhci_hc_driver, &pdev->dev,
				    dev_name(&pdev->dev));
	if (!tegra->hcd) {
		err = -ENOMEM;
		goto put_powerdomains;
	}

	tegra->hcd->skip_phy_initialization = 1;
	tegra->hcd->regs = tegra->regs;
	tegra->hcd->rsrc_start = regs->start;
	tegra->hcd->rsrc_len = resource_size(regs);

	/*
	 * This must happen after usb_create_hcd(), because usb_create_hcd()
	 * will overwrite the drvdata of the device with the hcd it creates.
	 */
	platform_set_drvdata(pdev, tegra);

	err = tegra_xusb_clk_enable(tegra);
	if (err) {
		dev_err(tegra->dev, "failed to enable clocks: %d\n", err);
		goto put_hcd;
	}

	err = regulator_bulk_enable(tegra->soc->num_supplies, tegra->supplies);
	if (err) {
		dev_err(tegra->dev, "failed to enable regulators: %d\n", err);
		goto disable_clk;
	}

	err = tegra_xusb_phy_enable(tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable PHYs: %d\n", err);
		goto disable_regulator;
	}

	/*
	 * The XUSB Falcon microcontroller can only address 40 bits, so set
	 * the DMA mask accordingly.
	 */
	err = dma_set_mask_and_coherent(tegra->dev, DMA_BIT_MASK(40));
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		goto disable_phy;
	}

	err = tegra_xusb_request_firmware(tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request firmware: %d\n", err);
		goto disable_phy;
	}

	err = tegra_xusb_unpowergate_partitions(tegra);
	if (err)
		goto free_firmware;

	tegra_xusb_config(tegra);

	err = tegra_xusb_load_firmware(tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to load firmware: %d\n", err);
		goto powergate;
	}

	err = usb_add_hcd(tegra->hcd, tegra->xhci_irq, IRQF_SHARED);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to add USB HCD: %d\n", err);
		goto powergate;
	}

	device_wakeup_enable(tegra->hcd->self.controller);

	xhci = hcd_to_xhci(tegra->hcd);

	xhci->shared_hcd = usb_create_shared_hcd(&tegra_xhci_hc_driver,
						 &pdev->dev,
						 dev_name(&pdev->dev),
						 tegra->hcd);
	if (!xhci->shared_hcd) {
		dev_err(&pdev->dev, "failed to create shared HCD\n");
		err = -ENOMEM;
		goto remove_usb2;
	}

	err = usb_add_hcd(xhci->shared_hcd, tegra->xhci_irq, IRQF_SHARED);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to add shared HCD: %d\n", err);
		goto put_usb3;
	}

	err = devm_request_threaded_irq(&pdev->dev, tegra->mbox_irq,
					tegra_xusb_mbox_irq,
					tegra_xusb_mbox_thread, 0,
					dev_name(&pdev->dev), tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", err);
		goto remove_usb3;
	}

	if (tegra->padctl_irq) {
		err = devm_request_threaded_irq(&pdev->dev, tegra->padctl_irq,
						NULL, tegra_xusb_padctl_irq,
						IRQF_ONESHOT, dev_name(&pdev->dev),
						tegra);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to request padctl IRQ: %d\n", err);
			goto remove_usb3;
		}
	}

	err = tegra_xusb_enable_firmware_messages(tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable messages: %d\n", err);
		goto remove_usb3;
	}

	err = tegra_xusb_init_usb_phy(tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init USB PHY: %d\n", err);
		goto remove_usb3;
	}

	/* Enable wake for both USB 2.0 and USB 3.0 roothubs */
	device_init_wakeup(&tegra->hcd->self.root_hub->dev, true);
	device_init_wakeup(&xhci->shared_hcd->self.root_hub->dev, true);

	pm_runtime_use_autosuspend(tegra->dev);
	pm_runtime_set_autosuspend_delay(tegra->dev, 2000);
	pm_runtime_mark_last_busy(tegra->dev);
	pm_runtime_set_active(tegra->dev);

	if (tegra->padctl_irq) {
		device_init_wakeup(tegra->dev, true);
		pm_runtime_enable(tegra->dev);
	}

	return 0;

remove_usb3:
	usb_remove_hcd(xhci->shared_hcd);
put_usb3:
	usb_put_hcd(xhci->shared_hcd);
remove_usb2:
	usb_remove_hcd(tegra->hcd);
powergate:
	tegra_xusb_powergate_partitions(tegra);
free_firmware:
	dma_free_coherent(&pdev->dev, tegra->fw.size, tegra->fw.virt,
			  tegra->fw.phys);
disable_phy:
	tegra_xusb_phy_disable(tegra);
disable_regulator:
	regulator_bulk_disable(tegra->soc->num_supplies, tegra->supplies);
disable_clk:
	tegra_xusb_clk_disable(tegra);
put_hcd:
	usb_put_hcd(tegra->hcd);
put_powerdomains:
	tegra_xusb_powerdomain_remove(&pdev->dev, tegra);
put_padctl:
	of_node_put(np);
	tegra_xusb_padctl_put(tegra->padctl);
	return err;
}

static int tegra_xusb_remove(struct platform_device *pdev)
{
	struct tegra_xusb *tegra = platform_get_drvdata(pdev);
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);

	tegra_xusb_deinit_usb_phy(tegra);

	pm_runtime_get_sync(&pdev->dev);
	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);
	xhci->shared_hcd = NULL;
	usb_remove_hcd(tegra->hcd);
	usb_put_hcd(tegra->hcd);

	dma_free_coherent(&pdev->dev, tegra->fw.size, tegra->fw.virt,
			  tegra->fw.phys);

	if (tegra->padctl_irq)
		pm_runtime_disable(&pdev->dev);

	pm_runtime_put(&pdev->dev);

	tegra_xusb_powergate_partitions(tegra);

	tegra_xusb_powerdomain_remove(&pdev->dev, tegra);

	tegra_xusb_phy_disable(tegra);
	tegra_xusb_clk_disable(tegra);
	regulator_bulk_disable(tegra->soc->num_supplies, tegra->supplies);
	tegra_xusb_padctl_put(tegra->padctl);

	return 0;
}

static bool xhci_hub_ports_suspended(struct xhci_hub *hub)
{
	struct device *dev = hub->hcd->self.controller;
	bool status = true;
	unsigned int i;
	u32 value;

	for (i = 0; i < hub->num_ports; i++) {
		value = readl(hub->ports[i]->addr);
		if ((value & PORT_PE) == 0)
			continue;

		if ((value & PORT_PLS_MASK) != XDEV_U3) {
			dev_info(dev, "%u-%u isn't suspended: %#010x\n",
				 hub->hcd->self.busnum, i + 1, value);
			status = false;
		}
	}

	return status;
}

static int tegra_xusb_check_ports(struct tegra_xusb *tegra)
{
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);
	struct xhci_bus_state *bus_state = &xhci->usb2_rhub.bus_state;
	unsigned long flags;
	int err = 0;

	if (bus_state->bus_suspended) {
		/* xusb_hub_suspend() has just directed one or more USB2 port(s)
		 * to U3 state, it takes 3ms to enter U3.
		 */
		usleep_range(3000, 4000);
	}

	spin_lock_irqsave(&xhci->lock, flags);

	if (!xhci_hub_ports_suspended(&xhci->usb2_rhub) ||
	    !xhci_hub_ports_suspended(&xhci->usb3_rhub))
		err = -EBUSY;

	spin_unlock_irqrestore(&xhci->lock, flags);

	return err;
}

static void tegra_xusb_save_context(struct tegra_xusb *tegra)
{
	const struct tegra_xusb_context_soc *soc = tegra->soc->context;
	struct tegra_xusb_context *ctx = &tegra->context;
	unsigned int i;

	if (soc->ipfs.num_offsets > 0) {
		for (i = 0; i < soc->ipfs.num_offsets; i++)
			ctx->ipfs[i] = ipfs_readl(tegra, soc->ipfs.offsets[i]);
	}

	if (soc->fpci.num_offsets > 0) {
		for (i = 0; i < soc->fpci.num_offsets; i++)
			ctx->fpci[i] = fpci_readl(tegra, soc->fpci.offsets[i]);
	}
}

static void tegra_xusb_restore_context(struct tegra_xusb *tegra)
{
	const struct tegra_xusb_context_soc *soc = tegra->soc->context;
	struct tegra_xusb_context *ctx = &tegra->context;
	unsigned int i;

	if (soc->fpci.num_offsets > 0) {
		for (i = 0; i < soc->fpci.num_offsets; i++)
			fpci_writel(tegra, ctx->fpci[i], soc->fpci.offsets[i]);
	}

	if (soc->ipfs.num_offsets > 0) {
		for (i = 0; i < soc->ipfs.num_offsets; i++)
			ipfs_writel(tegra, ctx->ipfs[i], soc->ipfs.offsets[i]);
	}
}

static enum usb_device_speed tegra_xhci_portsc_to_speed(struct tegra_xusb *tegra, u32 portsc)
{
	if (DEV_LOWSPEED(portsc))
		return USB_SPEED_LOW;

	if (DEV_HIGHSPEED(portsc))
		return USB_SPEED_HIGH;

	if (DEV_FULLSPEED(portsc))
		return USB_SPEED_FULL;

	if (DEV_SUPERSPEED_ANY(portsc))
		return USB_SPEED_SUPER;

	return USB_SPEED_UNKNOWN;
}

static void tegra_xhci_enable_phy_sleepwalk_wake(struct tegra_xusb *tegra)
{
	struct tegra_xusb_padctl *padctl = tegra->padctl;
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);
	enum usb_device_speed speed;
	struct phy *phy;
	unsigned int index, offset;
	unsigned int i, j, k;
	struct xhci_hub *rhub;
	u32 portsc;

	for (i = 0, k = 0; i < tegra->soc->num_types; i++) {
		if (strcmp(tegra->soc->phy_types[i].name, "usb3") == 0)
			rhub = &xhci->usb3_rhub;
		else
			rhub = &xhci->usb2_rhub;

		if (strcmp(tegra->soc->phy_types[i].name, "hsic") == 0)
			offset = tegra->soc->ports.usb2.count;
		else
			offset = 0;

		for (j = 0; j < tegra->soc->phy_types[i].num; j++) {
			phy = tegra->phys[k++];

			if (!phy)
				continue;

			index = j + offset;

			if (index >= rhub->num_ports)
				continue;

			if (!is_host_mode_phy(tegra, i, j))
				continue;

			portsc = readl(rhub->ports[index]->addr);
			speed = tegra_xhci_portsc_to_speed(tegra, portsc);
			tegra_xusb_padctl_enable_phy_sleepwalk(padctl, phy, speed);
			tegra_xusb_padctl_enable_phy_wake(padctl, phy);
		}
	}
}

static void tegra_xhci_disable_phy_wake(struct tegra_xusb *tegra)
{
	struct tegra_xusb_padctl *padctl = tegra->padctl;
	unsigned int i;

	for (i = 0; i < tegra->num_phys; i++) {
		if (!tegra->phys[i])
			continue;

		tegra_xusb_padctl_disable_phy_wake(padctl, tegra->phys[i]);
	}
}

static void tegra_xhci_disable_phy_sleepwalk(struct tegra_xusb *tegra)
{
	struct tegra_xusb_padctl *padctl = tegra->padctl;
	unsigned int i;

	for (i = 0; i < tegra->num_phys; i++) {
		if (!tegra->phys[i])
			continue;

		tegra_xusb_padctl_disable_phy_sleepwalk(padctl, tegra->phys[i]);
	}
}

static int tegra_xusb_enter_elpg(struct tegra_xusb *tegra, bool runtime)
{
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);
	struct device *dev = tegra->dev;
	bool wakeup = runtime ? true : device_may_wakeup(dev);
	unsigned int i;
	int err;
	u32 usbcmd;

	dev_dbg(dev, "entering ELPG\n");

	usbcmd = readl(&xhci->op_regs->command);
	usbcmd &= ~CMD_EIE;
	writel(usbcmd, &xhci->op_regs->command);

	err = tegra_xusb_check_ports(tegra);
	if (err < 0) {
		dev_err(tegra->dev, "not all ports suspended: %d\n", err);
		goto out;
	}

	err = xhci_suspend(xhci, wakeup);
	if (err < 0) {
		dev_err(tegra->dev, "failed to suspend XHCI: %d\n", err);
		goto out;
	}

	tegra_xusb_save_context(tegra);

	if (wakeup)
		tegra_xhci_enable_phy_sleepwalk_wake(tegra);

	tegra_xusb_powergate_partitions(tegra);

	for (i = 0; i < tegra->num_phys; i++) {
		if (!tegra->phys[i])
			continue;

		phy_power_off(tegra->phys[i]);
		if (!wakeup)
			phy_exit(tegra->phys[i]);
	}

	tegra_xusb_clk_disable(tegra);

out:
	if (!err)
		dev_dbg(tegra->dev, "entering ELPG done\n");
	else {
		usbcmd = readl(&xhci->op_regs->command);
		usbcmd |= CMD_EIE;
		writel(usbcmd, &xhci->op_regs->command);

		dev_dbg(tegra->dev, "entering ELPG failed\n");
		pm_runtime_mark_last_busy(tegra->dev);
	}

	return err;
}

static int tegra_xusb_exit_elpg(struct tegra_xusb *tegra, bool runtime)
{
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);
	struct device *dev = tegra->dev;
	bool wakeup = runtime ? true : device_may_wakeup(dev);
	unsigned int i;
	u32 usbcmd;
	int err;

	dev_dbg(dev, "exiting ELPG\n");
	pm_runtime_mark_last_busy(tegra->dev);

	err = tegra_xusb_clk_enable(tegra);
	if (err < 0) {
		dev_err(tegra->dev, "failed to enable clocks: %d\n", err);
		goto out;
	}

	err = tegra_xusb_unpowergate_partitions(tegra);
	if (err)
		goto disable_clks;

	if (wakeup)
		tegra_xhci_disable_phy_wake(tegra);

	for (i = 0; i < tegra->num_phys; i++) {
		if (!tegra->phys[i])
			continue;

		if (!wakeup)
			phy_init(tegra->phys[i]);

		phy_power_on(tegra->phys[i]);
	}

	tegra_xusb_config(tegra);
	tegra_xusb_restore_context(tegra);

	err = tegra_xusb_load_firmware(tegra);
	if (err < 0) {
		dev_err(tegra->dev, "failed to load firmware: %d\n", err);
		goto disable_phy;
	}

	err = __tegra_xusb_enable_firmware_messages(tegra);
	if (err < 0) {
		dev_err(tegra->dev, "failed to enable messages: %d\n", err);
		goto disable_phy;
	}

	if (wakeup)
		tegra_xhci_disable_phy_sleepwalk(tegra);

	err = xhci_resume(xhci, 0);
	if (err < 0) {
		dev_err(tegra->dev, "failed to resume XHCI: %d\n", err);
		goto disable_phy;
	}

	usbcmd = readl(&xhci->op_regs->command);
	usbcmd |= CMD_EIE;
	writel(usbcmd, &xhci->op_regs->command);

	goto out;

disable_phy:
	for (i = 0; i < tegra->num_phys; i++) {
		if (!tegra->phys[i])
			continue;

		phy_power_off(tegra->phys[i]);
		if (!wakeup)
			phy_exit(tegra->phys[i]);
	}
	tegra_xusb_powergate_partitions(tegra);
disable_clks:
	tegra_xusb_clk_disable(tegra);
out:
	if (!err)
		dev_dbg(dev, "exiting ELPG done\n");
	else
		dev_dbg(dev, "exiting ELPG failed\n");

	return err;
}

static __maybe_unused int tegra_xusb_suspend(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);
	int err;

	synchronize_irq(tegra->mbox_irq);

	mutex_lock(&tegra->lock);

	if (pm_runtime_suspended(dev)) {
		err = tegra_xusb_exit_elpg(tegra, true);
		if (err < 0)
			goto out;
	}

	err = tegra_xusb_enter_elpg(tegra, false);
	if (err < 0) {
		if (pm_runtime_suspended(dev)) {
			pm_runtime_disable(dev);
			pm_runtime_set_active(dev);
			pm_runtime_enable(dev);
		}

		goto out;
	}

out:
	if (!err) {
		tegra->suspended = true;
		pm_runtime_disable(dev);

		if (device_may_wakeup(dev)) {
			if (enable_irq_wake(tegra->padctl_irq))
				dev_err(dev, "failed to enable padctl wakes\n");
		}
	}

	mutex_unlock(&tegra->lock);

	return err;
}

static __maybe_unused int tegra_xusb_resume(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);
	int err;

	mutex_lock(&tegra->lock);

	if (!tegra->suspended) {
		mutex_unlock(&tegra->lock);
		return 0;
	}

	err = tegra_xusb_exit_elpg(tegra, false);
	if (err < 0) {
		mutex_unlock(&tegra->lock);
		return err;
	}

	if (device_may_wakeup(dev)) {
		if (disable_irq_wake(tegra->padctl_irq))
			dev_err(dev, "failed to disable padctl wakes\n");
	}
	tegra->suspended = false;
	mutex_unlock(&tegra->lock);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static __maybe_unused int tegra_xusb_runtime_suspend(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);
	int ret;

	synchronize_irq(tegra->mbox_irq);
	mutex_lock(&tegra->lock);
	ret = tegra_xusb_enter_elpg(tegra, true);
	mutex_unlock(&tegra->lock);

	return ret;
}

static __maybe_unused int tegra_xusb_runtime_resume(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);
	int err;

	mutex_lock(&tegra->lock);
	err = tegra_xusb_exit_elpg(tegra, true);
	mutex_unlock(&tegra->lock);

	return err;
}

static const struct dev_pm_ops tegra_xusb_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_xusb_runtime_suspend,
			   tegra_xusb_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_xusb_suspend, tegra_xusb_resume)
};

static const char * const tegra124_supply_names[] = {
	"avddio-pex",
	"dvddio-pex",
	"avdd-usb",
	"hvdd-usb-ss",
};

static const struct tegra_xusb_phy_type tegra124_phy_types[] = {
	{ .name = "usb3", .num = 2, },
	{ .name = "usb2", .num = 3, },
	{ .name = "hsic", .num = 2, },
};

static const unsigned int tegra124_xusb_context_ipfs[] = {
	IPFS_XUSB_HOST_MSI_BAR_SZ_0,
	IPFS_XUSB_HOST_MSI_AXI_BAR_ST_0,
	IPFS_XUSB_HOST_MSI_FPCI_BAR_ST_0,
	IPFS_XUSB_HOST_MSI_VEC0_0,
	IPFS_XUSB_HOST_MSI_EN_VEC0_0,
	IPFS_XUSB_HOST_FPCI_ERROR_MASKS_0,
	IPFS_XUSB_HOST_INTR_MASK_0,
	IPFS_XUSB_HOST_INTR_ENABLE_0,
	IPFS_XUSB_HOST_UFPCI_CONFIG_0,
	IPFS_XUSB_HOST_CLKGATE_HYSTERESIS_0,
	IPFS_XUSB_HOST_MCCIF_FIFOCTRL_0,
};

static const unsigned int tegra124_xusb_context_fpci[] = {
	XUSB_CFG_ARU_CONTEXT_HS_PLS,
	XUSB_CFG_ARU_CONTEXT_FS_PLS,
	XUSB_CFG_ARU_CONTEXT_HSFS_SPEED,
	XUSB_CFG_ARU_CONTEXT_HSFS_PP,
	XUSB_CFG_ARU_CONTEXT,
	XUSB_CFG_AXI_CFG,
	XUSB_CFG_24,
	XUSB_CFG_16,
};

static const struct tegra_xusb_context_soc tegra124_xusb_context = {
	.ipfs = {
		.num_offsets = ARRAY_SIZE(tegra124_xusb_context_ipfs),
		.offsets = tegra124_xusb_context_ipfs,
	},
	.fpci = {
		.num_offsets = ARRAY_SIZE(tegra124_xusb_context_fpci),
		.offsets = tegra124_xusb_context_fpci,
	},
};

static const struct tegra_xusb_soc tegra124_soc = {
	.firmware = "nvidia/tegra124/xusb.bin",
	.supply_names = tegra124_supply_names,
	.num_supplies = ARRAY_SIZE(tegra124_supply_names),
	.phy_types = tegra124_phy_types,
	.num_types = ARRAY_SIZE(tegra124_phy_types),
	.context = &tegra124_xusb_context,
	.ports = {
		.usb2 = { .offset = 4, .count = 4, },
		.hsic = { .offset = 6, .count = 2, },
		.usb3 = { .offset = 0, .count = 2, },
	},
	.scale_ss_clock = true,
	.has_ipfs = true,
	.otg_reset_sspi = false,
	.mbox = {
		.cmd = 0xe4,
		.data_in = 0xe8,
		.data_out = 0xec,
		.owner = 0xf0,
	},
};
MODULE_FIRMWARE("nvidia/tegra124/xusb.bin");

static const char * const tegra210_supply_names[] = {
	"dvddio-pex",
	"hvddio-pex",
	"avdd-usb",
};

static const struct tegra_xusb_phy_type tegra210_phy_types[] = {
	{ .name = "usb3", .num = 4, },
	{ .name = "usb2", .num = 4, },
	{ .name = "hsic", .num = 1, },
};

static const struct tegra_xusb_soc tegra210_soc = {
	.firmware = "nvidia/tegra210/xusb.bin",
	.supply_names = tegra210_supply_names,
	.num_supplies = ARRAY_SIZE(tegra210_supply_names),
	.phy_types = tegra210_phy_types,
	.num_types = ARRAY_SIZE(tegra210_phy_types),
	.context = &tegra124_xusb_context,
	.ports = {
		.usb2 = { .offset = 4, .count = 4, },
		.hsic = { .offset = 8, .count = 1, },
		.usb3 = { .offset = 0, .count = 4, },
	},
	.scale_ss_clock = false,
	.has_ipfs = true,
	.otg_reset_sspi = true,
	.mbox = {
		.cmd = 0xe4,
		.data_in = 0xe8,
		.data_out = 0xec,
		.owner = 0xf0,
	},
};
MODULE_FIRMWARE("nvidia/tegra210/xusb.bin");

static const char * const tegra186_supply_names[] = {
};
MODULE_FIRMWARE("nvidia/tegra186/xusb.bin");

static const struct tegra_xusb_phy_type tegra186_phy_types[] = {
	{ .name = "usb3", .num = 3, },
	{ .name = "usb2", .num = 3, },
	{ .name = "hsic", .num = 1, },
};

static const struct tegra_xusb_context_soc tegra186_xusb_context = {
	.fpci = {
		.num_offsets = ARRAY_SIZE(tegra124_xusb_context_fpci),
		.offsets = tegra124_xusb_context_fpci,
	},
};

static const struct tegra_xusb_soc tegra186_soc = {
	.firmware = "nvidia/tegra186/xusb.bin",
	.supply_names = tegra186_supply_names,
	.num_supplies = ARRAY_SIZE(tegra186_supply_names),
	.phy_types = tegra186_phy_types,
	.num_types = ARRAY_SIZE(tegra186_phy_types),
	.context = &tegra186_xusb_context,
	.ports = {
		.usb3 = { .offset = 0, .count = 3, },
		.usb2 = { .offset = 3, .count = 3, },
		.hsic = { .offset = 6, .count = 1, },
	},
	.scale_ss_clock = false,
	.has_ipfs = false,
	.otg_reset_sspi = false,
	.mbox = {
		.cmd = 0xe4,
		.data_in = 0xe8,
		.data_out = 0xec,
		.owner = 0xf0,
	},
	.lpm_support = true,
};

static const char * const tegra194_supply_names[] = {
};

static const struct tegra_xusb_phy_type tegra194_phy_types[] = {
	{ .name = "usb3", .num = 4, },
	{ .name = "usb2", .num = 4, },
};

static const struct tegra_xusb_soc tegra194_soc = {
	.firmware = "nvidia/tegra194/xusb.bin",
	.supply_names = tegra194_supply_names,
	.num_supplies = ARRAY_SIZE(tegra194_supply_names),
	.phy_types = tegra194_phy_types,
	.num_types = ARRAY_SIZE(tegra194_phy_types),
	.context = &tegra186_xusb_context,
	.ports = {
		.usb3 = { .offset = 0, .count = 4, },
		.usb2 = { .offset = 4, .count = 4, },
	},
	.scale_ss_clock = false,
	.has_ipfs = false,
	.otg_reset_sspi = false,
	.mbox = {
		.cmd = 0x68,
		.data_in = 0x6c,
		.data_out = 0x70,
		.owner = 0x74,
	},
	.lpm_support = true,
};
MODULE_FIRMWARE("nvidia/tegra194/xusb.bin");

static const struct of_device_id tegra_xusb_of_match[] = {
	{ .compatible = "nvidia,tegra124-xusb", .data = &tegra124_soc },
	{ .compatible = "nvidia,tegra210-xusb", .data = &tegra210_soc },
	{ .compatible = "nvidia,tegra186-xusb", .data = &tegra186_soc },
	{ .compatible = "nvidia,tegra194-xusb", .data = &tegra194_soc },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_xusb_of_match);

static struct platform_driver tegra_xusb_driver = {
	.probe = tegra_xusb_probe,
	.remove = tegra_xusb_remove,
	.driver = {
		.name = "tegra-xusb",
		.pm = &tegra_xusb_pm_ops,
		.of_match_table = tegra_xusb_of_match,
	},
};

static void tegra_xhci_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);

	xhci->quirks |= XHCI_PLAT;
	if (tegra && tegra->soc->lpm_support)
		xhci->quirks |= XHCI_LPM_SUPPORT;
}

static int tegra_xhci_setup(struct usb_hcd *hcd)
{
	return xhci_gen_setup(hcd, tegra_xhci_quirks);
}

static const struct xhci_driver_overrides tegra_xhci_overrides __initconst = {
	.reset = tegra_xhci_setup,
};

static int __init tegra_xusb_init(void)
{
	xhci_init_driver(&tegra_xhci_hc_driver, &tegra_xhci_overrides);

	return platform_driver_register(&tegra_xusb_driver);
}
module_init(tegra_xusb_init);

static void __exit tegra_xusb_exit(void)
{
	platform_driver_unregister(&tegra_xusb_driver);
}
module_exit(tegra_xusb_exit);

MODULE_AUTHOR("Andrew Bresticker <abrestic@chromium.org>");
MODULE_DESCRIPTION("NVIDIA Tegra XUSB xHCI host-controller driver");
MODULE_LICENSE("GPL v2");

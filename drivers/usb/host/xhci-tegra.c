// SPDX-License-Identifier: GPL-2.0
/*
 * NVIDIA Tegra xHCI host controller driver
 *
 * Copyright (C) 2014 NVIDIA Corporation
 * Copyright (C) 2014 Google, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/phy/tegra/xusb.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
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
#define XUSB_CFG_ARU_C11_CSBRANGE		0x41c
#define XUSB_CFG_CSB_BASE_ADDR			0x800

/* FPCI mailbox registers */
#define XUSB_CFG_ARU_MBOX_CMD			0x0e4
#define  MBOX_DEST_FALC				BIT(27)
#define  MBOX_DEST_PME				BIT(28)
#define  MBOX_DEST_SMI				BIT(29)
#define  MBOX_DEST_XHCI				BIT(30)
#define  MBOX_INT_EN				BIT(31)
#define XUSB_CFG_ARU_MBOX_DATA_IN		0x0e8
#define  CMD_DATA_SHIFT				0
#define  CMD_DATA_MASK				0xffffff
#define  CMD_TYPE_SHIFT				24
#define  CMD_TYPE_MASK				0xff
#define XUSB_CFG_ARU_MBOX_DATA_OUT		0x0ec
#define XUSB_CFG_ARU_MBOX_OWNER			0x0f0
#define  MBOX_OWNER_NONE			0
#define  MBOX_OWNER_FW				1
#define  MBOX_OWNER_SW				2
#define XUSB_CFG_ARU_SMI_INTR			0x428
#define  MBOX_SMI_INTR_FW_HANG			BIT(1)
#define  MBOX_SMI_INTR_EN			BIT(3)

/* IPFS registers */
#define IPFS_XUSB_HOST_CONFIGURATION_0		0x180
#define  IPFS_EN_FPCI				BIT(0)
#define IPFS_XUSB_HOST_INTR_MASK_0		0x188
#define  IPFS_IP_INT_MASK			BIT(16)
#define IPFS_XUSB_HOST_CLKGATE_HYSTERESIS_0	0x1bc

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

struct tegra_xusb_soc {
	const char *firmware;
	const char * const *supply_names;
	unsigned int num_supplies;
	const struct tegra_xusb_phy_type *phy_types;
	unsigned int num_types;

	struct {
		struct {
			unsigned int offset;
			unsigned int count;
		} usb2, ulpi, hsic, usb3;
	} ports;

	bool scale_ss_clock;
	bool has_ipfs;
};

struct tegra_xusb {
	struct device *dev;
	void __iomem *regs;
	struct usb_hcd *hcd;

	struct mutex lock;

	int xhci_irq;
	int mbox_irq;

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
	struct device_link *genpd_dl_host;
	struct device_link *genpd_dl_ss;

	struct phy **phys;
	unsigned int num_phys;

	/* Firmware loading related */
	struct {
		size_t size;
		void *virt;
		dma_addr_t phys;
	} fw;
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

static const char * const mbox_cmd_name[] = {
	[  1] = "MSG_ENABLE",
	[  2] = "INC_FALCON_CLOCK",
	[  3] = "DEC_FALCON_CLOCK",
	[  4] = "INC_SSPI_CLOCK",
	[  5] = "DEC_SSPI_CLOCK",
	[  6] = "SET_BW",
	[  7] = "SET_SS_PWR_GATING",
	[  8] = "SET_SS_PWR_UNGATING",
	[  9] = "SAVE_DFE_CTLE_CTX",
	[ 10] = "AIRPLANE_MODE_ENABLED",
	[ 11] = "AIRPLANE_MODE_DISABLED",
	[ 12] = "START_HSIC_IDLE",
	[ 13] = "STOP_HSIC_IDLE",
	[ 14] = "DBC_WAKE_STACK",
	[ 15] = "HSIC_PRETEND_CONNECT",
	[ 16] = "RESET_SSPI",
	[ 17] = "DISABLE_SS_LFPS_DETECTION",
	[ 18] = "ENABLE_SS_LFPS_DETECTION",
	[128] = "ACK",
	[129] = "NAK",
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
		value = fpci_readl(tegra, XUSB_CFG_ARU_MBOX_OWNER);
		if (value != MBOX_OWNER_NONE) {
			dev_err(tegra->dev, "mailbox is busy\n");
			return -EBUSY;
		}

		fpci_writel(tegra, MBOX_OWNER_SW, XUSB_CFG_ARU_MBOX_OWNER);

		value = fpci_readl(tegra, XUSB_CFG_ARU_MBOX_OWNER);
		if (value != MBOX_OWNER_SW) {
			dev_err(tegra->dev, "failed to acquire mailbox\n");
			return -EBUSY;
		}

		wait_for_idle = true;
	}

	value = tegra_xusb_mbox_pack(msg);
	fpci_writel(tegra, value, XUSB_CFG_ARU_MBOX_DATA_IN);

	value = fpci_readl(tegra, XUSB_CFG_ARU_MBOX_CMD);
	value |= MBOX_INT_EN | MBOX_DEST_FALC;
	fpci_writel(tegra, value, XUSB_CFG_ARU_MBOX_CMD);

	if (wait_for_idle) {
		unsigned long timeout = jiffies + msecs_to_jiffies(250);

		while (time_before(jiffies, timeout)) {
			value = fpci_readl(tegra, XUSB_CFG_ARU_MBOX_OWNER);
			if (value == MBOX_OWNER_NONE)
				break;

			usleep_range(10, 20);
		}

		if (time_after(jiffies, timeout))
			value = fpci_readl(tegra, XUSB_CFG_ARU_MBOX_OWNER);

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

	value = fpci_readl(tegra, XUSB_CFG_ARU_MBOX_DATA_OUT);
	tegra_xusb_mbox_unpack(&msg, value);

	value = fpci_readl(tegra, XUSB_CFG_ARU_MBOX_CMD);
	value &= ~MBOX_DEST_SMI;
	fpci_writel(tegra, value, XUSB_CFG_ARU_MBOX_CMD);

	/* clear mailbox owner if no ACK/NAK is required */
	if (!tegra_xusb_mbox_cmd_requires_ack(msg.cmd))
		fpci_writel(tegra, MBOX_OWNER_NONE, XUSB_CFG_ARU_MBOX_OWNER);

	tegra_xusb_mbox_handle(tegra, &msg);

	mutex_unlock(&tegra->lock);
	return IRQ_HANDLED;
}

static void tegra_xusb_config(struct tegra_xusb *tegra,
			      struct resource *regs)
{
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
	value |= regs->start & (XUSB_BASE_ADDR_MASK << XUSB_BASE_ADDR_SHIFT);
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

static int tegra_xusb_runtime_suspend(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);

	tegra_xusb_phy_disable(tegra);
	regulator_bulk_disable(tegra->soc->num_supplies, tegra->supplies);
	tegra_xusb_clk_disable(tegra);

	return 0;
}

static int tegra_xusb_runtime_resume(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);
	int err;

	err = tegra_xusb_clk_enable(tegra);
	if (err) {
		dev_err(dev, "failed to enable clocks: %d\n", err);
		return err;
	}

	err = regulator_bulk_enable(tegra->soc->num_supplies, tegra->supplies);
	if (err) {
		dev_err(dev, "failed to enable regulators: %d\n", err);
		goto disable_clk;
	}

	err = tegra_xusb_phy_enable(tegra);
	if (err < 0) {
		dev_err(dev, "failed to enable PHYs: %d\n", err);
		goto disable_regulator;
	}

	return 0;

disable_regulator:
	regulator_bulk_disable(tegra->soc->num_supplies, tegra->supplies);
disable_clk:
	tegra_xusb_clk_disable(tegra);
	return err;
}

static int tegra_xusb_load_firmware(struct tegra_xusb *tegra)
{
	unsigned int code_tag_blocks, code_size_blocks, code_blocks;
	struct tegra_xusb_fw_header *header;
	struct device *dev = tegra->dev;
	const struct firmware *fw;
	unsigned long timeout;
	time64_t timestamp;
	struct tm time;
	u64 address;
	u32 value;
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

	msleep(50);

	csb_writel(tegra, le32_to_cpu(header->boot_codetag),
		   XUSB_FALC_BOOTVEC);

	/* Boot Falcon CPU and wait for it to enter the STOPPED (idle) state. */
	timeout = jiffies + msecs_to_jiffies(5);

	csb_writel(tegra, CPUCTL_STARTCPU, XUSB_FALC_CPUCTL);

	while (time_before(jiffies, timeout)) {
		if (csb_readl(tegra, XUSB_FALC_CPUCTL) == CPUCTL_STATE_STOPPED)
			break;

		usleep_range(100, 200);
	}

	if (csb_readl(tegra, XUSB_FALC_CPUCTL) != CPUCTL_STATE_STOPPED) {
		dev_err(dev, "Falcon failed to start, state: %#x\n",
			csb_readl(tegra, XUSB_FALC_CPUCTL));
		return -EIO;
	}

	timestamp = le32_to_cpu(header->fwimg_created_time);
	time64_to_tm(timestamp, 0, &time);

	dev_info(dev, "Firmware timestamp: %ld-%02d-%02d %02d:%02d:%02d UTC\n",
		 time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
		 time.tm_hour, time.tm_min, time.tm_sec);

	return 0;
}

static void tegra_xusb_powerdomain_remove(struct device *dev,
					  struct tegra_xusb *tegra)
{
	if (tegra->genpd_dl_ss)
		device_link_del(tegra->genpd_dl_ss);
	if (tegra->genpd_dl_host)
		device_link_del(tegra->genpd_dl_host);
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

	tegra->genpd_dl_host = device_link_add(dev, tegra->genpd_dev_host,
					       DL_FLAG_PM_RUNTIME |
					       DL_FLAG_STATELESS);
	if (!tegra->genpd_dl_host) {
		dev_err(dev, "adding host device link failed!\n");
		return -ENODEV;
	}

	tegra->genpd_dl_ss = device_link_add(dev, tegra->genpd_dev_ss,
					     DL_FLAG_PM_RUNTIME |
					     DL_FLAG_STATELESS);
	if (!tegra->genpd_dl_ss) {
		dev_err(dev, "adding superspeed device link failed!\n");
		return -ENODEV;
	}

	return 0;
}

static int tegra_xusb_probe(struct platform_device *pdev)
{
	struct tegra_xusb_mbox_msg msg;
	struct resource *res, *regs;
	struct tegra_xusb *tegra;
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

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tegra->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(tegra->regs))
		return PTR_ERR(tegra->regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	tegra->fpci_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tegra->fpci_base))
		return PTR_ERR(tegra->fpci_base);

	if (tegra->soc->has_ipfs) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
		tegra->ipfs_base = devm_ioremap_resource(&pdev->dev, res);
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

		err = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_XUSBA,
							tegra->ss_clk,
							tegra->ss_rst);
		if (err) {
			dev_err(&pdev->dev,
				"failed to enable XUSBA domain: %d\n", err);
			goto put_padctl;
		}

		err = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_XUSBC,
							tegra->host_clk,
							tegra->host_rst);
		if (err) {
			tegra_powergate_power_off(TEGRA_POWERGATE_XUSBA);
			dev_err(&pdev->dev,
				"failed to enable XUSBC domain: %d\n", err);
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

	for (i = 0; i < tegra->soc->num_supplies; i++)
		tegra->supplies[i].supply = tegra->soc->supply_names[i];

	err = devm_regulator_bulk_get(&pdev->dev, tegra->soc->num_supplies,
				      tegra->supplies);
	if (err) {
		dev_err(&pdev->dev, "failed to get regulators: %d\n", err);
		goto put_powerdomains;
	}

	for (i = 0; i < tegra->soc->num_types; i++)
		tegra->num_phys += tegra->soc->phy_types[i].num;

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

	/*
	 * This must happen after usb_create_hcd(), because usb_create_hcd()
	 * will overwrite the drvdata of the device with the hcd it creates.
	 */
	platform_set_drvdata(pdev, tegra);

	pm_runtime_enable(&pdev->dev);
	if (pm_runtime_enabled(&pdev->dev))
		err = pm_runtime_get_sync(&pdev->dev);
	else
		err = tegra_xusb_runtime_resume(&pdev->dev);

	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable device: %d\n", err);
		goto disable_rpm;
	}

	tegra_xusb_config(tegra, regs);

	err = tegra_xusb_load_firmware(tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to load firmware: %d\n", err);
		goto put_rpm;
	}

	tegra->hcd->regs = tegra->regs;
	tegra->hcd->rsrc_start = regs->start;
	tegra->hcd->rsrc_len = resource_size(regs);

	err = usb_add_hcd(tegra->hcd, tegra->xhci_irq, IRQF_SHARED);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to add USB HCD: %d\n", err);
		goto put_rpm;
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

	mutex_lock(&tegra->lock);

	/* Enable firmware messages from controller. */
	msg.cmd = MBOX_CMD_MSG_ENABLED;
	msg.data = 0;

	err = tegra_xusb_mbox_send(tegra, &msg);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable messages: %d\n", err);
		mutex_unlock(&tegra->lock);
		goto remove_usb3;
	}

	mutex_unlock(&tegra->lock);

	err = devm_request_threaded_irq(&pdev->dev, tegra->mbox_irq,
					tegra_xusb_mbox_irq,
					tegra_xusb_mbox_thread, 0,
					dev_name(&pdev->dev), tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", err);
		goto remove_usb3;
	}

	return 0;

remove_usb3:
	usb_remove_hcd(xhci->shared_hcd);
put_usb3:
	usb_put_hcd(xhci->shared_hcd);
remove_usb2:
	usb_remove_hcd(tegra->hcd);
put_rpm:
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_xusb_runtime_suspend(&pdev->dev);
disable_rpm:
	pm_runtime_disable(&pdev->dev);
	usb_put_hcd(tegra->hcd);
put_powerdomains:
	if (!of_property_read_bool(pdev->dev.of_node, "power-domains")) {
		tegra_powergate_power_off(TEGRA_POWERGATE_XUSBC);
		tegra_powergate_power_off(TEGRA_POWERGATE_XUSBA);
	} else {
		tegra_xusb_powerdomain_remove(&pdev->dev, tegra);
	}
put_padctl:
	tegra_xusb_padctl_put(tegra->padctl);
	return err;
}

static int tegra_xusb_remove(struct platform_device *pdev)
{
	struct tegra_xusb *tegra = platform_get_drvdata(pdev);
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);

	usb_remove_hcd(xhci->shared_hcd);
	usb_put_hcd(xhci->shared_hcd);
	xhci->shared_hcd = NULL;
	usb_remove_hcd(tegra->hcd);
	usb_put_hcd(tegra->hcd);

	dma_free_coherent(&pdev->dev, tegra->fw.size, tegra->fw.virt,
			  tegra->fw.phys);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	if (!of_property_read_bool(pdev->dev.of_node, "power-domains")) {
		tegra_powergate_power_off(TEGRA_POWERGATE_XUSBC);
		tegra_powergate_power_off(TEGRA_POWERGATE_XUSBA);
	} else {
		tegra_xusb_powerdomain_remove(&pdev->dev, tegra);
	}

	tegra_xusb_padctl_put(tegra->padctl);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_xusb_suspend(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);
	bool wakeup = device_may_wakeup(dev);

	/* TODO: Powergate controller across suspend/resume. */
	return xhci_suspend(xhci, wakeup);
}

static int tegra_xusb_resume(struct device *dev)
{
	struct tegra_xusb *tegra = dev_get_drvdata(dev);
	struct xhci_hcd *xhci = hcd_to_xhci(tegra->hcd);

	return xhci_resume(xhci, 0);
}
#endif

static const struct dev_pm_ops tegra_xusb_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_xusb_runtime_suspend,
			   tegra_xusb_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_xusb_suspend, tegra_xusb_resume)
};

static const char * const tegra124_supply_names[] = {
	"avddio-pex",
	"dvddio-pex",
	"avdd-usb",
	"avdd-pll-utmip",
	"avdd-pll-erefe",
	"avdd-usb-ss-pll",
	"hvdd-usb-ss",
	"hvdd-usb-ss-pll-e",
};

static const struct tegra_xusb_phy_type tegra124_phy_types[] = {
	{ .name = "usb3", .num = 2, },
	{ .name = "usb2", .num = 3, },
	{ .name = "hsic", .num = 2, },
};

static const struct tegra_xusb_soc tegra124_soc = {
	.firmware = "nvidia/tegra124/xusb.bin",
	.supply_names = tegra124_supply_names,
	.num_supplies = ARRAY_SIZE(tegra124_supply_names),
	.phy_types = tegra124_phy_types,
	.num_types = ARRAY_SIZE(tegra124_phy_types),
	.ports = {
		.usb2 = { .offset = 4, .count = 4, },
		.hsic = { .offset = 6, .count = 2, },
		.usb3 = { .offset = 0, .count = 2, },
	},
	.scale_ss_clock = true,
	.has_ipfs = true,
};
MODULE_FIRMWARE("nvidia/tegra124/xusb.bin");

static const char * const tegra210_supply_names[] = {
	"dvddio-pex",
	"hvddio-pex",
	"avdd-usb",
	"avdd-pll-utmip",
	"avdd-pll-uerefe",
	"dvdd-pex-pll",
	"hvdd-pex-pll-e",
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
	.ports = {
		.usb2 = { .offset = 4, .count = 4, },
		.hsic = { .offset = 8, .count = 1, },
		.usb3 = { .offset = 0, .count = 4, },
	},
	.scale_ss_clock = false,
	.has_ipfs = true,
};
MODULE_FIRMWARE("nvidia/tegra210/xusb.bin");

static const char * const tegra186_supply_names[] = {
};

static const struct tegra_xusb_phy_type tegra186_phy_types[] = {
	{ .name = "usb3", .num = 3, },
	{ .name = "usb2", .num = 3, },
	{ .name = "hsic", .num = 1, },
};

static const struct tegra_xusb_soc tegra186_soc = {
	.firmware = "nvidia/tegra186/xusb.bin",
	.supply_names = tegra186_supply_names,
	.num_supplies = ARRAY_SIZE(tegra186_supply_names),
	.phy_types = tegra186_phy_types,
	.num_types = ARRAY_SIZE(tegra186_phy_types),
	.ports = {
		.usb3 = { .offset = 0, .count = 3, },
		.usb2 = { .offset = 3, .count = 3, },
		.hsic = { .offset = 6, .count = 1, },
	},
	.scale_ss_clock = false,
	.has_ipfs = false,
};

static const struct of_device_id tegra_xusb_of_match[] = {
	{ .compatible = "nvidia,tegra124-xusb", .data = &tegra124_soc },
	{ .compatible = "nvidia,tegra210-xusb", .data = &tegra210_soc },
	{ .compatible = "nvidia,tegra186-xusb", .data = &tegra186_soc },
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
	xhci->quirks |= XHCI_PLAT;
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

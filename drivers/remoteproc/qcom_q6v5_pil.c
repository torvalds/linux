/*
 * Qualcomm Peripheral Image Loader
 *
 * Copyright (C) 2016 Linaro Ltd.
 * Copyright (C) 2014 Sony Mobile Communications AB
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/iopoll.h>

#include "remoteproc_internal.h"
#include "qcom_common.h"

#include <linux/qcom_scm.h>

#define MPSS_CRASH_REASON_SMEM		421

/* RMB Status Register Values */
#define RMB_PBL_SUCCESS			0x1

#define RMB_MBA_XPU_UNLOCKED		0x1
#define RMB_MBA_XPU_UNLOCKED_SCRIBBLED	0x2
#define RMB_MBA_META_DATA_AUTH_SUCCESS	0x3
#define RMB_MBA_AUTH_COMPLETE		0x4

/* PBL/MBA interface registers */
#define RMB_MBA_IMAGE_REG		0x00
#define RMB_PBL_STATUS_REG		0x04
#define RMB_MBA_COMMAND_REG		0x08
#define RMB_MBA_STATUS_REG		0x0C
#define RMB_PMI_META_DATA_REG		0x10
#define RMB_PMI_CODE_START_REG		0x14
#define RMB_PMI_CODE_LENGTH_REG		0x18

#define RMB_CMD_META_DATA_READY		0x1
#define RMB_CMD_LOAD_READY		0x2

/* QDSP6SS Register Offsets */
#define QDSP6SS_RESET_REG		0x014
#define QDSP6SS_GFMUX_CTL_REG		0x020
#define QDSP6SS_PWR_CTL_REG		0x030
#define QDSP6SS_MEM_PWR_CTL		0x0B0
#define QDSP6SS_STRAP_ACC		0x110

/* AXI Halt Register Offsets */
#define AXI_HALTREQ_REG			0x0
#define AXI_HALTACK_REG			0x4
#define AXI_IDLE_REG			0x8

#define HALT_ACK_TIMEOUT_MS		100

/* QDSP6SS_RESET */
#define Q6SS_STOP_CORE			BIT(0)
#define Q6SS_CORE_ARES			BIT(1)
#define Q6SS_BUS_ARES_ENABLE		BIT(2)

/* QDSP6SS_GFMUX_CTL */
#define Q6SS_CLK_ENABLE			BIT(1)

/* QDSP6SS_PWR_CTL */
#define Q6SS_L2DATA_SLP_NRET_N_0	BIT(0)
#define Q6SS_L2DATA_SLP_NRET_N_1	BIT(1)
#define Q6SS_L2DATA_SLP_NRET_N_2	BIT(2)
#define Q6SS_L2TAG_SLP_NRET_N		BIT(16)
#define Q6SS_ETB_SLP_NRET_N		BIT(17)
#define Q6SS_L2DATA_STBY_N		BIT(18)
#define Q6SS_SLP_RET_N			BIT(19)
#define Q6SS_CLAMP_IO			BIT(20)
#define QDSS_BHS_ON			BIT(21)
#define QDSS_LDO_BYP			BIT(22)

/* QDSP6v56 parameters */
#define QDSP6v56_LDO_BYP		BIT(25)
#define QDSP6v56_BHS_ON		BIT(24)
#define QDSP6v56_CLAMP_WL		BIT(21)
#define QDSP6v56_CLAMP_QMC_MEM		BIT(22)
#define HALT_CHECK_MAX_LOOPS		200
#define QDSP6SS_XO_CBCR		0x0038
#define QDSP6SS_ACC_OVERRIDE_VAL		0x20

struct reg_info {
	struct regulator *reg;
	int uV;
	int uA;
};

struct qcom_mss_reg_res {
	const char *supply;
	int uV;
	int uA;
};

struct rproc_hexagon_res {
	const char *hexagon_mba_image;
	struct qcom_mss_reg_res *proxy_supply;
	struct qcom_mss_reg_res *active_supply;
	char **proxy_clk_names;
	char **active_clk_names;
	int version;
	bool need_mem_protection;
};

struct q6v5 {
	struct device *dev;
	struct rproc *rproc;

	void __iomem *reg_base;
	void __iomem *rmb_base;

	struct regmap *halt_map;
	u32 halt_q6;
	u32 halt_modem;
	u32 halt_nc;

	struct reset_control *mss_restart;

	struct qcom_smem_state *state;
	unsigned stop_bit;

	struct clk *active_clks[8];
	struct clk *proxy_clks[4];
	int active_clk_count;
	int proxy_clk_count;

	struct reg_info active_regs[1];
	struct reg_info proxy_regs[3];
	int active_reg_count;
	int proxy_reg_count;

	struct completion start_done;
	struct completion stop_done;
	bool running;

	phys_addr_t mba_phys;
	void *mba_region;
	size_t mba_size;

	phys_addr_t mpss_phys;
	phys_addr_t mpss_reloc;
	void *mpss_region;
	size_t mpss_size;

	struct qcom_rproc_subdev smd_subdev;
	struct qcom_rproc_ssr ssr_subdev;
	struct qcom_sysmon *sysmon;
	bool need_mem_protection;
	int mpss_perm;
	int mba_perm;
	int version;
};

enum {
	MSS_MSM8916,
	MSS_MSM8974,
	MSS_MSM8996,
};

static int q6v5_regulator_init(struct device *dev, struct reg_info *regs,
			       const struct qcom_mss_reg_res *reg_res)
{
	int rc;
	int i;

	if (!reg_res)
		return 0;

	for (i = 0; reg_res[i].supply; i++) {
		regs[i].reg = devm_regulator_get(dev, reg_res[i].supply);
		if (IS_ERR(regs[i].reg)) {
			rc = PTR_ERR(regs[i].reg);
			if (rc != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s\n regulator",
					reg_res[i].supply);
			return rc;
		}

		regs[i].uV = reg_res[i].uV;
		regs[i].uA = reg_res[i].uA;
	}

	return i;
}

static int q6v5_regulator_enable(struct q6v5 *qproc,
				 struct reg_info *regs, int count)
{
	int ret;
	int i;

	for (i = 0; i < count; i++) {
		if (regs[i].uV > 0) {
			ret = regulator_set_voltage(regs[i].reg,
					regs[i].uV, INT_MAX);
			if (ret) {
				dev_err(qproc->dev,
					"Failed to request voltage for %d.\n",
						i);
				goto err;
			}
		}

		if (regs[i].uA > 0) {
			ret = regulator_set_load(regs[i].reg,
						 regs[i].uA);
			if (ret < 0) {
				dev_err(qproc->dev,
					"Failed to set regulator mode\n");
				goto err;
			}
		}

		ret = regulator_enable(regs[i].reg);
		if (ret) {
			dev_err(qproc->dev, "Regulator enable failed\n");
			goto err;
		}
	}

	return 0;
err:
	for (; i >= 0; i--) {
		if (regs[i].uV > 0)
			regulator_set_voltage(regs[i].reg, 0, INT_MAX);

		if (regs[i].uA > 0)
			regulator_set_load(regs[i].reg, 0);

		regulator_disable(regs[i].reg);
	}

	return ret;
}

static void q6v5_regulator_disable(struct q6v5 *qproc,
				   struct reg_info *regs, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (regs[i].uV > 0)
			regulator_set_voltage(regs[i].reg, 0, INT_MAX);

		if (regs[i].uA > 0)
			regulator_set_load(regs[i].reg, 0);

		regulator_disable(regs[i].reg);
	}
}

static int q6v5_clk_enable(struct device *dev,
			   struct clk **clks, int count)
{
	int rc;
	int i;

	for (i = 0; i < count; i++) {
		rc = clk_prepare_enable(clks[i]);
		if (rc) {
			dev_err(dev, "Clock enable failed\n");
			goto err;
		}
	}

	return 0;
err:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(clks[i]);

	return rc;
}

static void q6v5_clk_disable(struct device *dev,
			     struct clk **clks, int count)
{
	int i;

	for (i = 0; i < count; i++)
		clk_disable_unprepare(clks[i]);
}

static int q6v5_xfer_mem_ownership(struct q6v5 *qproc, int *current_perm,
				   bool remote_owner, phys_addr_t addr,
				   size_t size)
{
	struct qcom_scm_vmperm next;

	if (!qproc->need_mem_protection)
		return 0;
	if (remote_owner && *current_perm == BIT(QCOM_SCM_VMID_MSS_MSA))
		return 0;
	if (!remote_owner && *current_perm == BIT(QCOM_SCM_VMID_HLOS))
		return 0;

	next.vmid = remote_owner ? QCOM_SCM_VMID_MSS_MSA : QCOM_SCM_VMID_HLOS;
	next.perm = remote_owner ? QCOM_SCM_PERM_RW : QCOM_SCM_PERM_RWX;

	return qcom_scm_assign_mem(addr, ALIGN(size, SZ_4K),
				   current_perm, &next, 1);
}

static int q6v5_load(struct rproc *rproc, const struct firmware *fw)
{
	struct q6v5 *qproc = rproc->priv;

	memcpy(qproc->mba_region, fw->data, fw->size);

	return 0;
}

static int q6v5_rmb_pbl_wait(struct q6v5 *qproc, int ms)
{
	unsigned long timeout;
	s32 val;

	timeout = jiffies + msecs_to_jiffies(ms);
	for (;;) {
		val = readl(qproc->rmb_base + RMB_PBL_STATUS_REG);
		if (val)
			break;

		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

		msleep(1);
	}

	return val;
}

static int q6v5_rmb_mba_wait(struct q6v5 *qproc, u32 status, int ms)
{

	unsigned long timeout;
	s32 val;

	timeout = jiffies + msecs_to_jiffies(ms);
	for (;;) {
		val = readl(qproc->rmb_base + RMB_MBA_STATUS_REG);
		if (val < 0)
			break;

		if (!status && val)
			break;
		else if (status && val == status)
			break;

		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

		msleep(1);
	}

	return val;
}

static int q6v5proc_reset(struct q6v5 *qproc)
{
	u32 val;
	int ret;
	int i;


	if (qproc->version == MSS_MSM8996) {
		/* Override the ACC value if required */
		writel(QDSP6SS_ACC_OVERRIDE_VAL,
		       qproc->reg_base + QDSP6SS_STRAP_ACC);

		/* Assert resets, stop core */
		val = readl(qproc->reg_base + QDSP6SS_RESET_REG);
		val |= Q6SS_CORE_ARES | Q6SS_BUS_ARES_ENABLE | Q6SS_STOP_CORE;
		writel(val, qproc->reg_base + QDSP6SS_RESET_REG);

		/* BHS require xo cbcr to be enabled */
		val = readl(qproc->reg_base + QDSP6SS_XO_CBCR);
		val |= 0x1;
		writel(val, qproc->reg_base + QDSP6SS_XO_CBCR);

		/* Read CLKOFF bit to go low indicating CLK is enabled */
		ret = readl_poll_timeout(qproc->reg_base + QDSP6SS_XO_CBCR,
					 val, !(val & BIT(31)), 1,
					 HALT_CHECK_MAX_LOOPS);
		if (ret) {
			dev_err(qproc->dev,
				"xo cbcr enabling timed out (rc:%d)\n", ret);
			return ret;
		}
		/* Enable power block headswitch and wait for it to stabilize */
		val = readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= QDSP6v56_BHS_ON;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		udelay(1);

		/* Put LDO in bypass mode */
		val |= QDSP6v56_LDO_BYP;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);

		/* Deassert QDSP6 compiler memory clamp */
		val = readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val &= ~QDSP6v56_CLAMP_QMC_MEM;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);

		/* Deassert memory peripheral sleep and L2 memory standby */
		val |= Q6SS_L2DATA_STBY_N | Q6SS_SLP_RET_N;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);

		/* Turn on L1, L2, ETB and JU memories 1 at a time */
		val = readl(qproc->reg_base + QDSP6SS_MEM_PWR_CTL);
		for (i = 19; i >= 0; i--) {
			val |= BIT(i);
			writel(val, qproc->reg_base +
						QDSP6SS_MEM_PWR_CTL);
			/*
			 * Read back value to ensure the write is done then
			 * wait for 1us for both memory peripheral and data
			 * array to turn on.
			 */
			val |= readl(qproc->reg_base + QDSP6SS_MEM_PWR_CTL);
			udelay(1);
		}
		/* Remove word line clamp */
		val = readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val &= ~QDSP6v56_CLAMP_WL;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
	} else {
		/* Assert resets, stop core */
		val = readl(qproc->reg_base + QDSP6SS_RESET_REG);
		val |= Q6SS_CORE_ARES | Q6SS_BUS_ARES_ENABLE | Q6SS_STOP_CORE;
		writel(val, qproc->reg_base + QDSP6SS_RESET_REG);

		/* Enable power block headswitch and wait for it to stabilize */
		val = readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= QDSS_BHS_ON | QDSS_LDO_BYP;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		udelay(1);
		/*
		 * Turn on memories. L2 banks should be done individually
		 * to minimize inrush current.
		 */
		val = readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= Q6SS_SLP_RET_N | Q6SS_L2TAG_SLP_NRET_N |
			Q6SS_ETB_SLP_NRET_N | Q6SS_L2DATA_STBY_N;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= Q6SS_L2DATA_SLP_NRET_N_2;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= Q6SS_L2DATA_SLP_NRET_N_1;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= Q6SS_L2DATA_SLP_NRET_N_0;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
	}
	/* Remove IO clamp */
	val &= ~Q6SS_CLAMP_IO;
	writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);

	/* Bring core out of reset */
	val = readl(qproc->reg_base + QDSP6SS_RESET_REG);
	val &= ~Q6SS_CORE_ARES;
	writel(val, qproc->reg_base + QDSP6SS_RESET_REG);

	/* Turn on core clock */
	val = readl(qproc->reg_base + QDSP6SS_GFMUX_CTL_REG);
	val |= Q6SS_CLK_ENABLE;
	writel(val, qproc->reg_base + QDSP6SS_GFMUX_CTL_REG);

	/* Start core execution */
	val = readl(qproc->reg_base + QDSP6SS_RESET_REG);
	val &= ~Q6SS_STOP_CORE;
	writel(val, qproc->reg_base + QDSP6SS_RESET_REG);

	/* Wait for PBL status */
	ret = q6v5_rmb_pbl_wait(qproc, 1000);
	if (ret == -ETIMEDOUT) {
		dev_err(qproc->dev, "PBL boot timed out\n");
	} else if (ret != RMB_PBL_SUCCESS) {
		dev_err(qproc->dev, "PBL returned unexpected status %d\n", ret);
		ret = -EINVAL;
	} else {
		ret = 0;
	}

	return ret;
}

static void q6v5proc_halt_axi_port(struct q6v5 *qproc,
				   struct regmap *halt_map,
				   u32 offset)
{
	unsigned long timeout;
	unsigned int val;
	int ret;

	/* Check if we're already idle */
	ret = regmap_read(halt_map, offset + AXI_IDLE_REG, &val);
	if (!ret && val)
		return;

	/* Assert halt request */
	regmap_write(halt_map, offset + AXI_HALTREQ_REG, 1);

	/* Wait for halt */
	timeout = jiffies + msecs_to_jiffies(HALT_ACK_TIMEOUT_MS);
	for (;;) {
		ret = regmap_read(halt_map, offset + AXI_HALTACK_REG, &val);
		if (ret || val || time_after(jiffies, timeout))
			break;

		msleep(1);
	}

	ret = regmap_read(halt_map, offset + AXI_IDLE_REG, &val);
	if (ret || !val)
		dev_err(qproc->dev, "port failed halt\n");

	/* Clear halt request (port will remain halted until reset) */
	regmap_write(halt_map, offset + AXI_HALTREQ_REG, 0);
}

static int q6v5_mpss_init_image(struct q6v5 *qproc, const struct firmware *fw)
{
	unsigned long dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	dma_addr_t phys;
	int mdata_perm;
	int xferop_ret;
	void *ptr;
	int ret;

	ptr = dma_alloc_attrs(qproc->dev, fw->size, &phys, GFP_KERNEL, dma_attrs);
	if (!ptr) {
		dev_err(qproc->dev, "failed to allocate mdt buffer\n");
		return -ENOMEM;
	}

	memcpy(ptr, fw->data, fw->size);

	/* Hypervisor mapping to access metadata by modem */
	mdata_perm = BIT(QCOM_SCM_VMID_HLOS);
	ret = q6v5_xfer_mem_ownership(qproc, &mdata_perm,
				      true, phys, fw->size);
	if (ret) {
		dev_err(qproc->dev,
			"assigning Q6 access to metadata failed: %d\n", ret);
		ret = -EAGAIN;
		goto free_dma_attrs;
	}

	writel(phys, qproc->rmb_base + RMB_PMI_META_DATA_REG);
	writel(RMB_CMD_META_DATA_READY, qproc->rmb_base + RMB_MBA_COMMAND_REG);

	ret = q6v5_rmb_mba_wait(qproc, RMB_MBA_META_DATA_AUTH_SUCCESS, 1000);
	if (ret == -ETIMEDOUT)
		dev_err(qproc->dev, "MPSS header authentication timed out\n");
	else if (ret < 0)
		dev_err(qproc->dev, "MPSS header authentication failed: %d\n", ret);

	/* Metadata authentication done, remove modem access */
	xferop_ret = q6v5_xfer_mem_ownership(qproc, &mdata_perm,
					     false, phys, fw->size);
	if (xferop_ret)
		dev_warn(qproc->dev,
			 "mdt buffer not reclaimed system may become unstable\n");

free_dma_attrs:
	dma_free_attrs(qproc->dev, fw->size, ptr, phys, dma_attrs);

	return ret < 0 ? ret : 0;
}

static bool q6v5_phdr_valid(const struct elf32_phdr *phdr)
{
	if (phdr->p_type != PT_LOAD)
		return false;

	if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
		return false;

	if (!phdr->p_memsz)
		return false;

	return true;
}

static int q6v5_mpss_load(struct q6v5 *qproc)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct firmware *seg_fw;
	const struct firmware *fw;
	struct elf32_hdr *ehdr;
	phys_addr_t mpss_reloc;
	phys_addr_t boot_addr;
	phys_addr_t min_addr = (phys_addr_t)ULLONG_MAX;
	phys_addr_t max_addr = 0;
	bool relocate = false;
	char seg_name[10];
	ssize_t offset;
	size_t size = 0;
	void *ptr;
	int ret;
	int i;

	ret = request_firmware(&fw, "modem.mdt", qproc->dev);
	if (ret < 0) {
		dev_err(qproc->dev, "unable to load modem.mdt\n");
		return ret;
	}

	/* Initialize the RMB validator */
	writel(0, qproc->rmb_base + RMB_PMI_CODE_LENGTH_REG);

	ret = q6v5_mpss_init_image(qproc, fw);
	if (ret)
		goto release_firmware;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!q6v5_phdr_valid(phdr))
			continue;

		if (phdr->p_flags & QCOM_MDT_RELOCATABLE)
			relocate = true;

		if (phdr->p_paddr < min_addr)
			min_addr = phdr->p_paddr;

		if (phdr->p_paddr + phdr->p_memsz > max_addr)
			max_addr = ALIGN(phdr->p_paddr + phdr->p_memsz, SZ_4K);
	}

	mpss_reloc = relocate ? min_addr : qproc->mpss_phys;
	/* Load firmware segments */
	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (!q6v5_phdr_valid(phdr))
			continue;

		offset = phdr->p_paddr - mpss_reloc;
		if (offset < 0 || offset + phdr->p_memsz > qproc->mpss_size) {
			dev_err(qproc->dev, "segment outside memory range\n");
			ret = -EINVAL;
			goto release_firmware;
		}

		ptr = qproc->mpss_region + offset;

		if (phdr->p_filesz) {
			snprintf(seg_name, sizeof(seg_name), "modem.b%02d", i);
			ret = request_firmware(&seg_fw, seg_name, qproc->dev);
			if (ret) {
				dev_err(qproc->dev, "failed to load %s\n", seg_name);
				goto release_firmware;
			}

			memcpy(ptr, seg_fw->data, seg_fw->size);

			release_firmware(seg_fw);
		}

		if (phdr->p_memsz > phdr->p_filesz) {
			memset(ptr + phdr->p_filesz, 0,
			       phdr->p_memsz - phdr->p_filesz);
		}
		size += phdr->p_memsz;
	}

	/* Transfer ownership of modem ddr region to q6 */
	ret = q6v5_xfer_mem_ownership(qproc, &qproc->mpss_perm, true,
				      qproc->mpss_phys, qproc->mpss_size);
	if (ret) {
		dev_err(qproc->dev,
			"assigning Q6 access to mpss memory failed: %d\n", ret);
		ret = -EAGAIN;
		goto release_firmware;
	}

	boot_addr = relocate ? qproc->mpss_phys : min_addr;
	writel(boot_addr, qproc->rmb_base + RMB_PMI_CODE_START_REG);
	writel(RMB_CMD_LOAD_READY, qproc->rmb_base + RMB_MBA_COMMAND_REG);
	writel(size, qproc->rmb_base + RMB_PMI_CODE_LENGTH_REG);

	ret = q6v5_rmb_mba_wait(qproc, RMB_MBA_AUTH_COMPLETE, 10000);
	if (ret == -ETIMEDOUT)
		dev_err(qproc->dev, "MPSS authentication timed out\n");
	else if (ret < 0)
		dev_err(qproc->dev, "MPSS authentication failed: %d\n", ret);

release_firmware:
	release_firmware(fw);

	return ret < 0 ? ret : 0;
}

static int q6v5_start(struct rproc *rproc)
{
	struct q6v5 *qproc = (struct q6v5 *)rproc->priv;
	int xfermemop_ret;
	int ret;

	ret = q6v5_regulator_enable(qproc, qproc->proxy_regs,
				    qproc->proxy_reg_count);
	if (ret) {
		dev_err(qproc->dev, "failed to enable proxy supplies\n");
		return ret;
	}

	ret = q6v5_clk_enable(qproc->dev, qproc->proxy_clks,
			      qproc->proxy_clk_count);
	if (ret) {
		dev_err(qproc->dev, "failed to enable proxy clocks\n");
		goto disable_proxy_reg;
	}

	ret = q6v5_regulator_enable(qproc, qproc->active_regs,
				    qproc->active_reg_count);
	if (ret) {
		dev_err(qproc->dev, "failed to enable supplies\n");
		goto disable_proxy_clk;
	}
	ret = reset_control_deassert(qproc->mss_restart);
	if (ret) {
		dev_err(qproc->dev, "failed to deassert mss restart\n");
		goto disable_vdd;
	}

	ret = q6v5_clk_enable(qproc->dev, qproc->active_clks,
			      qproc->active_clk_count);
	if (ret) {
		dev_err(qproc->dev, "failed to enable clocks\n");
		goto assert_reset;
	}

	/* Assign MBA image access in DDR to q6 */
	ret = q6v5_xfer_mem_ownership(qproc, &qproc->mba_perm, true,
				      qproc->mba_phys, qproc->mba_size);
	if (ret) {
		dev_err(qproc->dev,
			"assigning Q6 access to mba memory failed: %d\n", ret);
		goto disable_active_clks;
	}

	writel(qproc->mba_phys, qproc->rmb_base + RMB_MBA_IMAGE_REG);

	ret = q6v5proc_reset(qproc);
	if (ret)
		goto reclaim_mba;

	ret = q6v5_rmb_mba_wait(qproc, 0, 5000);
	if (ret == -ETIMEDOUT) {
		dev_err(qproc->dev, "MBA boot timed out\n");
		goto halt_axi_ports;
	} else if (ret != RMB_MBA_XPU_UNLOCKED &&
		   ret != RMB_MBA_XPU_UNLOCKED_SCRIBBLED) {
		dev_err(qproc->dev, "MBA returned unexpected status %d\n", ret);
		ret = -EINVAL;
		goto halt_axi_ports;
	}

	dev_info(qproc->dev, "MBA booted, loading mpss\n");

	ret = q6v5_mpss_load(qproc);
	if (ret)
		goto reclaim_mpss;

	ret = wait_for_completion_timeout(&qproc->start_done,
					  msecs_to_jiffies(5000));
	if (ret == 0) {
		dev_err(qproc->dev, "start timed out\n");
		ret = -ETIMEDOUT;
		goto reclaim_mpss;
	}

	xfermemop_ret = q6v5_xfer_mem_ownership(qproc, &qproc->mba_perm, false,
						qproc->mba_phys,
						qproc->mba_size);
	if (xfermemop_ret)
		dev_err(qproc->dev,
			"Failed to reclaim mba buffer system may become unstable\n");
	qproc->running = true;

	q6v5_clk_disable(qproc->dev, qproc->proxy_clks,
			 qproc->proxy_clk_count);
	q6v5_regulator_disable(qproc, qproc->proxy_regs,
			       qproc->proxy_reg_count);

	return 0;

reclaim_mpss:
	xfermemop_ret = q6v5_xfer_mem_ownership(qproc, &qproc->mpss_perm,
						false, qproc->mpss_phys,
						qproc->mpss_size);
	WARN_ON(xfermemop_ret);

halt_axi_ports:
	q6v5proc_halt_axi_port(qproc, qproc->halt_map, qproc->halt_q6);
	q6v5proc_halt_axi_port(qproc, qproc->halt_map, qproc->halt_modem);
	q6v5proc_halt_axi_port(qproc, qproc->halt_map, qproc->halt_nc);

reclaim_mba:
	xfermemop_ret = q6v5_xfer_mem_ownership(qproc, &qproc->mba_perm, false,
						qproc->mba_phys,
						qproc->mba_size);
	if (xfermemop_ret) {
		dev_err(qproc->dev,
			"Failed to reclaim mba buffer, system may become unstable\n");
	}

disable_active_clks:
	q6v5_clk_disable(qproc->dev, qproc->active_clks,
			 qproc->active_clk_count);

assert_reset:
	reset_control_assert(qproc->mss_restart);
disable_vdd:
	q6v5_regulator_disable(qproc, qproc->active_regs,
			       qproc->active_reg_count);
disable_proxy_clk:
	q6v5_clk_disable(qproc->dev, qproc->proxy_clks,
			 qproc->proxy_clk_count);
disable_proxy_reg:
	q6v5_regulator_disable(qproc, qproc->proxy_regs,
			       qproc->proxy_reg_count);

	return ret;
}

static int q6v5_stop(struct rproc *rproc)
{
	struct q6v5 *qproc = (struct q6v5 *)rproc->priv;
	int ret;
	u32 val;

	qproc->running = false;

	qcom_smem_state_update_bits(qproc->state,
				    BIT(qproc->stop_bit), BIT(qproc->stop_bit));

	ret = wait_for_completion_timeout(&qproc->stop_done,
					  msecs_to_jiffies(5000));
	if (ret == 0)
		dev_err(qproc->dev, "timed out on wait\n");

	qcom_smem_state_update_bits(qproc->state, BIT(qproc->stop_bit), 0);

	q6v5proc_halt_axi_port(qproc, qproc->halt_map, qproc->halt_q6);
	q6v5proc_halt_axi_port(qproc, qproc->halt_map, qproc->halt_modem);
	q6v5proc_halt_axi_port(qproc, qproc->halt_map, qproc->halt_nc);
	if (qproc->version == MSS_MSM8996) {
		/*
		 * To avoid high MX current during LPASS/MSS restart.
		 */
		val = readl(qproc->reg_base + QDSP6SS_PWR_CTL_REG);
		val |= Q6SS_CLAMP_IO | QDSP6v56_CLAMP_WL |
			QDSP6v56_CLAMP_QMC_MEM;
		writel(val, qproc->reg_base + QDSP6SS_PWR_CTL_REG);
	}


	ret = q6v5_xfer_mem_ownership(qproc, &qproc->mpss_perm, false,
				      qproc->mpss_phys, qproc->mpss_size);
	WARN_ON(ret);

	reset_control_assert(qproc->mss_restart);
	q6v5_clk_disable(qproc->dev, qproc->active_clks,
			 qproc->active_clk_count);
	q6v5_regulator_disable(qproc, qproc->active_regs,
			       qproc->active_reg_count);

	return 0;
}

static void *q6v5_da_to_va(struct rproc *rproc, u64 da, int len)
{
	struct q6v5 *qproc = rproc->priv;
	int offset;

	offset = da - qproc->mpss_reloc;
	if (offset < 0 || offset + len > qproc->mpss_size)
		return NULL;

	return qproc->mpss_region + offset;
}

static const struct rproc_ops q6v5_ops = {
	.start = q6v5_start,
	.stop = q6v5_stop,
	.da_to_va = q6v5_da_to_va,
	.load = q6v5_load,
};

static irqreturn_t q6v5_wdog_interrupt(int irq, void *dev)
{
	struct q6v5 *qproc = dev;
	size_t len;
	char *msg;

	/* Sometimes the stop triggers a watchdog rather than a stop-ack */
	if (!qproc->running) {
		complete(&qproc->stop_done);
		return IRQ_HANDLED;
	}

	msg = qcom_smem_get(QCOM_SMEM_HOST_ANY, MPSS_CRASH_REASON_SMEM, &len);
	if (!IS_ERR(msg) && len > 0 && msg[0])
		dev_err(qproc->dev, "watchdog received: %s\n", msg);
	else
		dev_err(qproc->dev, "watchdog without message\n");

	rproc_report_crash(qproc->rproc, RPROC_WATCHDOG);

	return IRQ_HANDLED;
}

static irqreturn_t q6v5_fatal_interrupt(int irq, void *dev)
{
	struct q6v5 *qproc = dev;
	size_t len;
	char *msg;

	msg = qcom_smem_get(QCOM_SMEM_HOST_ANY, MPSS_CRASH_REASON_SMEM, &len);
	if (!IS_ERR(msg) && len > 0 && msg[0])
		dev_err(qproc->dev, "fatal error received: %s\n", msg);
	else
		dev_err(qproc->dev, "fatal error without message\n");

	rproc_report_crash(qproc->rproc, RPROC_FATAL_ERROR);

	return IRQ_HANDLED;
}

static irqreturn_t q6v5_handover_interrupt(int irq, void *dev)
{
	struct q6v5 *qproc = dev;

	complete(&qproc->start_done);
	return IRQ_HANDLED;
}

static irqreturn_t q6v5_stop_ack_interrupt(int irq, void *dev)
{
	struct q6v5 *qproc = dev;

	complete(&qproc->stop_done);
	return IRQ_HANDLED;
}

static int q6v5_init_mem(struct q6v5 *qproc, struct platform_device *pdev)
{
	struct of_phandle_args args;
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qdsp6");
	qproc->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(qproc->reg_base))
		return PTR_ERR(qproc->reg_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rmb");
	qproc->rmb_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(qproc->rmb_base))
		return PTR_ERR(qproc->rmb_base);

	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
					       "qcom,halt-regs", 3, 0, &args);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to parse qcom,halt-regs\n");
		return -EINVAL;
	}

	qproc->halt_map = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	if (IS_ERR(qproc->halt_map))
		return PTR_ERR(qproc->halt_map);

	qproc->halt_q6 = args.args[0];
	qproc->halt_modem = args.args[1];
	qproc->halt_nc = args.args[2];

	return 0;
}

static int q6v5_init_clocks(struct device *dev, struct clk **clks,
		char **clk_names)
{
	int i;

	if (!clk_names)
		return 0;

	for (i = 0; clk_names[i]; i++) {
		clks[i] = devm_clk_get(dev, clk_names[i]);
		if (IS_ERR(clks[i])) {
			int rc = PTR_ERR(clks[i]);

			if (rc != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s clock\n",
					clk_names[i]);
			return rc;
		}
	}

	return i;
}

static int q6v5_init_reset(struct q6v5 *qproc)
{
	qproc->mss_restart = devm_reset_control_get_exclusive(qproc->dev,
							      NULL);
	if (IS_ERR(qproc->mss_restart)) {
		dev_err(qproc->dev, "failed to acquire mss restart\n");
		return PTR_ERR(qproc->mss_restart);
	}

	return 0;
}

static int q6v5_request_irq(struct q6v5 *qproc,
			     struct platform_device *pdev,
			     const char *name,
			     irq_handler_t thread_fn)
{
	int ret;

	ret = platform_get_irq_byname(pdev, name);
	if (ret < 0) {
		dev_err(&pdev->dev, "no %s IRQ defined\n", name);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, ret,
					NULL, thread_fn,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"q6v5", qproc);
	if (ret)
		dev_err(&pdev->dev, "request %s IRQ failed\n", name);

	return ret;
}

static int q6v5_alloc_memory_region(struct q6v5 *qproc)
{
	struct device_node *child;
	struct device_node *node;
	struct resource r;
	int ret;

	child = of_get_child_by_name(qproc->dev->of_node, "mba");
	node = of_parse_phandle(child, "memory-region", 0);
	ret = of_address_to_resource(node, 0, &r);
	if (ret) {
		dev_err(qproc->dev, "unable to resolve mba region\n");
		return ret;
	}
	of_node_put(node);

	qproc->mba_phys = r.start;
	qproc->mba_size = resource_size(&r);
	qproc->mba_region = devm_ioremap_wc(qproc->dev, qproc->mba_phys, qproc->mba_size);
	if (!qproc->mba_region) {
		dev_err(qproc->dev, "unable to map memory region: %pa+%zx\n",
			&r.start, qproc->mba_size);
		return -EBUSY;
	}

	child = of_get_child_by_name(qproc->dev->of_node, "mpss");
	node = of_parse_phandle(child, "memory-region", 0);
	ret = of_address_to_resource(node, 0, &r);
	if (ret) {
		dev_err(qproc->dev, "unable to resolve mpss region\n");
		return ret;
	}
	of_node_put(node);

	qproc->mpss_phys = qproc->mpss_reloc = r.start;
	qproc->mpss_size = resource_size(&r);
	qproc->mpss_region = devm_ioremap_wc(qproc->dev, qproc->mpss_phys, qproc->mpss_size);
	if (!qproc->mpss_region) {
		dev_err(qproc->dev, "unable to map memory region: %pa+%zx\n",
			&r.start, qproc->mpss_size);
		return -EBUSY;
	}

	return 0;
}

static int q6v5_probe(struct platform_device *pdev)
{
	const struct rproc_hexagon_res *desc;
	struct q6v5 *qproc;
	struct rproc *rproc;
	int ret;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	rproc = rproc_alloc(&pdev->dev, pdev->name, &q6v5_ops,
			    desc->hexagon_mba_image, sizeof(*qproc));
	if (!rproc) {
		dev_err(&pdev->dev, "failed to allocate rproc\n");
		return -ENOMEM;
	}

	qproc = (struct q6v5 *)rproc->priv;
	qproc->dev = &pdev->dev;
	qproc->rproc = rproc;
	platform_set_drvdata(pdev, qproc);

	init_completion(&qproc->start_done);
	init_completion(&qproc->stop_done);

	ret = q6v5_init_mem(qproc, pdev);
	if (ret)
		goto free_rproc;

	ret = q6v5_alloc_memory_region(qproc);
	if (ret)
		goto free_rproc;

	ret = q6v5_init_clocks(&pdev->dev, qproc->proxy_clks,
			       desc->proxy_clk_names);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get proxy clocks.\n");
		goto free_rproc;
	}
	qproc->proxy_clk_count = ret;

	ret = q6v5_init_clocks(&pdev->dev, qproc->active_clks,
			       desc->active_clk_names);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get active clocks.\n");
		goto free_rproc;
	}
	qproc->active_clk_count = ret;

	ret = q6v5_regulator_init(&pdev->dev, qproc->proxy_regs,
				  desc->proxy_supply);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get proxy regulators.\n");
		goto free_rproc;
	}
	qproc->proxy_reg_count = ret;

	ret = q6v5_regulator_init(&pdev->dev,  qproc->active_regs,
				  desc->active_supply);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get active regulators.\n");
		goto free_rproc;
	}
	qproc->active_reg_count = ret;

	ret = q6v5_init_reset(qproc);
	if (ret)
		goto free_rproc;

	qproc->version = desc->version;
	qproc->need_mem_protection = desc->need_mem_protection;
	ret = q6v5_request_irq(qproc, pdev, "wdog", q6v5_wdog_interrupt);
	if (ret < 0)
		goto free_rproc;

	ret = q6v5_request_irq(qproc, pdev, "fatal", q6v5_fatal_interrupt);
	if (ret < 0)
		goto free_rproc;

	ret = q6v5_request_irq(qproc, pdev, "handover", q6v5_handover_interrupt);
	if (ret < 0)
		goto free_rproc;

	ret = q6v5_request_irq(qproc, pdev, "stop-ack", q6v5_stop_ack_interrupt);
	if (ret < 0)
		goto free_rproc;

	qproc->state = qcom_smem_state_get(&pdev->dev, "stop", &qproc->stop_bit);
	if (IS_ERR(qproc->state)) {
		ret = PTR_ERR(qproc->state);
		goto free_rproc;
	}
	qproc->mpss_perm = BIT(QCOM_SCM_VMID_HLOS);
	qproc->mba_perm = BIT(QCOM_SCM_VMID_HLOS);
	qcom_add_smd_subdev(rproc, &qproc->smd_subdev);
	qcom_add_ssr_subdev(rproc, &qproc->ssr_subdev, "mpss");
	qproc->sysmon = qcom_add_sysmon_subdev(rproc, "modem", 0x12);

	ret = rproc_add(rproc);
	if (ret)
		goto free_rproc;

	return 0;

free_rproc:
	rproc_free(rproc);

	return ret;
}

static int q6v5_remove(struct platform_device *pdev)
{
	struct q6v5 *qproc = platform_get_drvdata(pdev);

	rproc_del(qproc->rproc);

	qcom_remove_sysmon_subdev(qproc->sysmon);
	qcom_remove_smd_subdev(qproc->rproc, &qproc->smd_subdev);
	qcom_remove_ssr_subdev(qproc->rproc, &qproc->ssr_subdev);
	rproc_free(qproc->rproc);

	return 0;
}

static const struct rproc_hexagon_res msm8996_mss = {
	.hexagon_mba_image = "mba.mbn",
	.proxy_clk_names = (char*[]){
			"xo",
			"pnoc",
			NULL
	},
	.active_clk_names = (char*[]){
			"iface",
			"bus",
			"mem",
			"gpll0_mss_clk",
			NULL
	},
	.need_mem_protection = true,
	.version = MSS_MSM8996,
};

static const struct rproc_hexagon_res msm8916_mss = {
	.hexagon_mba_image = "mba.mbn",
	.proxy_supply = (struct qcom_mss_reg_res[]) {
		{
			.supply = "mx",
			.uV = 1050000,
		},
		{
			.supply = "cx",
			.uA = 100000,
		},
		{
			.supply = "pll",
			.uA = 100000,
		},
		{}
	},
	.proxy_clk_names = (char*[]){
		"xo",
		NULL
	},
	.active_clk_names = (char*[]){
		"iface",
		"bus",
		"mem",
		NULL
	},
	.need_mem_protection = false,
	.version = MSS_MSM8916,
};

static const struct rproc_hexagon_res msm8974_mss = {
	.hexagon_mba_image = "mba.b00",
	.proxy_supply = (struct qcom_mss_reg_res[]) {
		{
			.supply = "mx",
			.uV = 1050000,
		},
		{
			.supply = "cx",
			.uA = 100000,
		},
		{
			.supply = "pll",
			.uA = 100000,
		},
		{}
	},
	.active_supply = (struct qcom_mss_reg_res[]) {
		{
			.supply = "mss",
			.uV = 1050000,
			.uA = 100000,
		},
		{}
	},
	.proxy_clk_names = (char*[]){
		"xo",
		NULL
	},
	.active_clk_names = (char*[]){
		"iface",
		"bus",
		"mem",
		NULL
	},
	.need_mem_protection = false,
	.version = MSS_MSM8974,
};

static const struct of_device_id q6v5_of_match[] = {
	{ .compatible = "qcom,q6v5-pil", .data = &msm8916_mss},
	{ .compatible = "qcom,msm8916-mss-pil", .data = &msm8916_mss},
	{ .compatible = "qcom,msm8974-mss-pil", .data = &msm8974_mss},
	{ .compatible = "qcom,msm8996-mss-pil", .data = &msm8996_mss},
	{ },
};
MODULE_DEVICE_TABLE(of, q6v5_of_match);

static struct platform_driver q6v5_driver = {
	.probe = q6v5_probe,
	.remove = q6v5_remove,
	.driver = {
		.name = "qcom-q6v5-pil",
		.of_match_table = q6v5_of_match,
	},
};
module_platform_driver(q6v5_driver);

MODULE_DESCRIPTION("Peripheral Image Loader for Hexagon");
MODULE_LICENSE("GPL v2");

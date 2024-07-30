// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 MediaTek Inc.

#include <asm/barrier.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_scp.h>
#include <linux/rpmsg/mtk_rpmsg.h>

#include "mtk_common.h"
#include "remoteproc_internal.h"

#define SECTION_NAME_IPI_BUFFER ".ipi_buffer"

/**
 * scp_get() - get a reference to SCP.
 *
 * @pdev:	the platform device of the module requesting SCP platform
 *		device for using SCP API.
 *
 * Return: Return NULL if failed.  otherwise reference to SCP.
 **/
struct mtk_scp *scp_get(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *scp_node;
	struct platform_device *scp_pdev;

	scp_node = of_parse_phandle(dev->of_node, "mediatek,scp", 0);
	if (!scp_node) {
		dev_err(dev, "can't get SCP node\n");
		return NULL;
	}

	scp_pdev = of_find_device_by_node(scp_node);
	of_node_put(scp_node);

	if (WARN_ON(!scp_pdev)) {
		dev_err(dev, "SCP pdev failed\n");
		return NULL;
	}

	return platform_get_drvdata(scp_pdev);
}
EXPORT_SYMBOL_GPL(scp_get);

/**
 * scp_put() - "free" the SCP
 *
 * @scp:	mtk_scp structure from scp_get().
 **/
void scp_put(struct mtk_scp *scp)
{
	put_device(scp->dev);
}
EXPORT_SYMBOL_GPL(scp_put);

static void scp_wdt_handler(struct mtk_scp *scp, u32 scp_to_host)
{
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;
	struct mtk_scp *scp_node;

	dev_err(scp->dev, "SCP watchdog timeout! 0x%x", scp_to_host);

	/* report watchdog timeout to all cores */
	list_for_each_entry(scp_node, &scp_cluster->mtk_scp_list, elem)
		rproc_report_crash(scp_node->rproc, RPROC_WATCHDOG);
}

static void scp_init_ipi_handler(void *data, unsigned int len, void *priv)
{
	struct mtk_scp *scp = priv;
	struct scp_run *run = data;

	scp->run.signaled = run->signaled;
	strscpy(scp->run.fw_ver, run->fw_ver, SCP_FW_VER_LEN);
	scp->run.dec_capability = run->dec_capability;
	scp->run.enc_capability = run->enc_capability;
	wake_up_interruptible(&scp->run.wq);
}

static void scp_ipi_handler(struct mtk_scp *scp)
{
	struct mtk_share_obj __iomem *rcv_obj = scp->recv_buf;
	struct scp_ipi_desc *ipi_desc = scp->ipi_desc;
	scp_ipi_handler_t handler;
	u32 id = readl(&rcv_obj->id);
	u32 len = readl(&rcv_obj->len);
	const struct mtk_scp_sizes_data *scp_sizes;

	scp_sizes = scp->data->scp_sizes;
	if (len > scp_sizes->ipi_share_buffer_size) {
		dev_err(scp->dev, "ipi message too long (len %d, max %zd)", len,
			scp_sizes->ipi_share_buffer_size);
		return;
	}
	if (id >= SCP_IPI_MAX) {
		dev_err(scp->dev, "No such ipi id = %d\n", id);
		return;
	}

	scp_ipi_lock(scp, id);
	handler = ipi_desc[id].handler;
	if (!handler) {
		dev_err(scp->dev, "No handler for ipi id = %d\n", id);
		scp_ipi_unlock(scp, id);
		return;
	}

	memcpy_fromio(scp->share_buf, &rcv_obj->share_buf, len);
	memset(&scp->share_buf[len], 0, scp_sizes->ipi_share_buffer_size - len);
	handler(scp->share_buf, len, ipi_desc[id].priv);
	scp_ipi_unlock(scp, id);

	scp->ipi_id_ack[id] = true;
	wake_up(&scp->ack_wq);
}

static int scp_elf_read_ipi_buf_addr(struct mtk_scp *scp,
				     const struct firmware *fw,
				     size_t *offset);

static int scp_ipi_init(struct mtk_scp *scp, const struct firmware *fw)
{
	int ret;
	size_t buf_sz, offset;
	size_t share_buf_offset;
	const struct mtk_scp_sizes_data *scp_sizes;

	/* read the ipi buf addr from FW itself first */
	ret = scp_elf_read_ipi_buf_addr(scp, fw, &offset);
	if (ret) {
		/* use default ipi buf addr if the FW doesn't have it */
		offset = scp->data->ipi_buf_offset;
		if (!offset)
			return ret;
	}
	dev_info(scp->dev, "IPI buf addr %#010zx\n", offset);

	/* Make sure IPI buffer fits in the L2TCM range assigned to this core */
	buf_sz = sizeof(*scp->recv_buf) + sizeof(*scp->send_buf);

	if (scp->sram_size < buf_sz + offset) {
		dev_err(scp->dev, "IPI buffer does not fit in SRAM.\n");
		return -EOVERFLOW;
	}

	scp_sizes = scp->data->scp_sizes;
	scp->recv_buf = (struct mtk_share_obj __iomem *)
			(scp->sram_base + offset);
	share_buf_offset = sizeof(scp->recv_buf->id)
		+ sizeof(scp->recv_buf->len) + scp_sizes->ipi_share_buffer_size;
	scp->send_buf = (struct mtk_share_obj __iomem *)
			(scp->sram_base + offset + share_buf_offset);
	memset_io(scp->recv_buf, 0, share_buf_offset);
	memset_io(scp->send_buf, 0, share_buf_offset);

	return 0;
}

static void mt8183_scp_reset_assert(struct mtk_scp *scp)
{
	u32 val;

	val = readl(scp->cluster->reg_base + MT8183_SW_RSTN);
	val &= ~MT8183_SW_RSTN_BIT;
	writel(val, scp->cluster->reg_base + MT8183_SW_RSTN);
}

static void mt8183_scp_reset_deassert(struct mtk_scp *scp)
{
	u32 val;

	val = readl(scp->cluster->reg_base + MT8183_SW_RSTN);
	val |= MT8183_SW_RSTN_BIT;
	writel(val, scp->cluster->reg_base + MT8183_SW_RSTN);
}

static void mt8192_scp_reset_assert(struct mtk_scp *scp)
{
	writel(1, scp->cluster->reg_base + MT8192_CORE0_SW_RSTN_SET);
}

static void mt8192_scp_reset_deassert(struct mtk_scp *scp)
{
	writel(1, scp->cluster->reg_base + MT8192_CORE0_SW_RSTN_CLR);
}

static void mt8195_scp_c1_reset_assert(struct mtk_scp *scp)
{
	writel(1, scp->cluster->reg_base + MT8195_CORE1_SW_RSTN_SET);
}

static void mt8195_scp_c1_reset_deassert(struct mtk_scp *scp)
{
	writel(1, scp->cluster->reg_base + MT8195_CORE1_SW_RSTN_CLR);
}

static void mt8183_scp_irq_handler(struct mtk_scp *scp)
{
	u32 scp_to_host;

	scp_to_host = readl(scp->cluster->reg_base + MT8183_SCP_TO_HOST);
	if (scp_to_host & MT8183_SCP_IPC_INT_BIT)
		scp_ipi_handler(scp);
	else
		scp_wdt_handler(scp, scp_to_host);

	/* SCP won't send another interrupt until we set SCP_TO_HOST to 0. */
	writel(MT8183_SCP_IPC_INT_BIT | MT8183_SCP_WDT_INT_BIT,
	       scp->cluster->reg_base + MT8183_SCP_TO_HOST);
}

static void mt8192_scp_irq_handler(struct mtk_scp *scp)
{
	u32 scp_to_host;

	scp_to_host = readl(scp->cluster->reg_base + MT8192_SCP2APMCU_IPC_SET);

	if (scp_to_host & MT8192_SCP_IPC_INT_BIT) {
		scp_ipi_handler(scp);

		/*
		 * SCP won't send another interrupt until we clear
		 * MT8192_SCP2APMCU_IPC.
		 */
		writel(MT8192_SCP_IPC_INT_BIT,
		       scp->cluster->reg_base + MT8192_SCP2APMCU_IPC_CLR);
	} else {
		scp_wdt_handler(scp, scp_to_host);
		writel(1, scp->cluster->reg_base + MT8192_CORE0_WDT_IRQ);
	}
}

static void mt8195_scp_irq_handler(struct mtk_scp *scp)
{
	u32 scp_to_host;

	scp_to_host = readl(scp->cluster->reg_base + MT8192_SCP2APMCU_IPC_SET);

	if (scp_to_host & MT8192_SCP_IPC_INT_BIT) {
		scp_ipi_handler(scp);
	} else {
		u32 reason = readl(scp->cluster->reg_base + MT8195_SYS_STATUS);

		if (reason & MT8195_CORE0_WDT)
			writel(1, scp->cluster->reg_base + MT8192_CORE0_WDT_IRQ);

		if (reason & MT8195_CORE1_WDT)
			writel(1, scp->cluster->reg_base + MT8195_CORE1_WDT_IRQ);

		scp_wdt_handler(scp, reason);
	}

	writel(scp_to_host, scp->cluster->reg_base + MT8192_SCP2APMCU_IPC_CLR);
}

static void mt8195_scp_c1_irq_handler(struct mtk_scp *scp)
{
	u32 scp_to_host;

	scp_to_host = readl(scp->cluster->reg_base + MT8195_SSHUB2APMCU_IPC_SET);

	if (scp_to_host & MT8192_SCP_IPC_INT_BIT)
		scp_ipi_handler(scp);

	writel(scp_to_host, scp->cluster->reg_base + MT8195_SSHUB2APMCU_IPC_CLR);
}

static irqreturn_t scp_irq_handler(int irq, void *priv)
{
	struct mtk_scp *scp = priv;
	int ret;

	ret = clk_prepare_enable(scp->clk);
	if (ret) {
		dev_err(scp->dev, "failed to enable clocks\n");
		return IRQ_NONE;
	}

	scp->data->scp_irq_handler(scp);

	clk_disable_unprepare(scp->clk);

	return IRQ_HANDLED;
}

static int scp_elf_load_segments(struct rproc *rproc, const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	struct elf32_hdr *ehdr;
	struct elf32_phdr *phdr;
	int i, ret = 0;
	const u8 *elf_data = fw->data;

	ehdr = (struct elf32_hdr *)elf_data;
	phdr = (struct elf32_phdr *)(elf_data + ehdr->e_phoff);

	/* go through the available ELF segments */
	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {
		u32 da = phdr->p_paddr;
		u32 memsz = phdr->p_memsz;
		u32 filesz = phdr->p_filesz;
		u32 offset = phdr->p_offset;
		void __iomem *ptr;

		dev_dbg(dev, "phdr: type %d da 0x%x memsz 0x%x filesz 0x%x\n",
			phdr->p_type, da, memsz, filesz);

		if (phdr->p_type != PT_LOAD)
			continue;
		if (!filesz)
			continue;

		if (filesz > memsz) {
			dev_err(dev, "bad phdr filesz 0x%x memsz 0x%x\n",
				filesz, memsz);
			ret = -EINVAL;
			break;
		}

		if (offset + filesz > fw->size) {
			dev_err(dev, "truncated fw: need 0x%x avail 0x%zx\n",
				offset + filesz, fw->size);
			ret = -EINVAL;
			break;
		}

		/* grab the kernel address for this device address */
		ptr = (void __iomem *)rproc_da_to_va(rproc, da, memsz, NULL);
		if (!ptr) {
			dev_err(dev, "bad phdr da 0x%x mem 0x%x\n", da, memsz);
			ret = -EINVAL;
			break;
		}

		/* put the segment where the remote processor expects it */
		scp_memcpy_aligned(ptr, elf_data + phdr->p_offset, filesz);
	}

	return ret;
}

static int scp_elf_read_ipi_buf_addr(struct mtk_scp *scp,
				     const struct firmware *fw,
				     size_t *offset)
{
	struct elf32_hdr *ehdr;
	struct elf32_shdr *shdr, *shdr_strtab;
	int i;
	const u8 *elf_data = fw->data;
	const char *strtab;

	ehdr = (struct elf32_hdr *)elf_data;
	shdr = (struct elf32_shdr *)(elf_data + ehdr->e_shoff);
	shdr_strtab = shdr + ehdr->e_shstrndx;
	strtab = (const char *)(elf_data + shdr_strtab->sh_offset);

	for (i = 0; i < ehdr->e_shnum; i++, shdr++) {
		if (strcmp(strtab + shdr->sh_name,
			   SECTION_NAME_IPI_BUFFER) == 0) {
			*offset = shdr->sh_addr;
			return 0;
		}
	}

	return -ENOENT;
}

static int mt8183_scp_clk_get(struct mtk_scp *scp)
{
	struct device *dev = scp->dev;
	int ret = 0;

	scp->clk = devm_clk_get(dev, "main");
	if (IS_ERR(scp->clk)) {
		dev_err(dev, "Failed to get clock\n");
		ret = PTR_ERR(scp->clk);
	}

	return ret;
}

static int mt8192_scp_clk_get(struct mtk_scp *scp)
{
	return mt8183_scp_clk_get(scp);
}

static int mt8195_scp_clk_get(struct mtk_scp *scp)
{
	scp->clk = NULL;

	return 0;
}

static int mt8183_scp_before_load(struct mtk_scp *scp)
{
	/* Clear SCP to host interrupt */
	writel(MT8183_SCP_IPC_INT_BIT, scp->cluster->reg_base + MT8183_SCP_TO_HOST);

	/* Reset clocks before loading FW */
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_CLK_SW_SEL);
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_CLK_DIV_SEL);

	/* Initialize TCM before loading FW. */
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_L1_SRAM_PD);
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_TCM_TAIL_SRAM_PD);

	/* Turn on the power of SCP's SRAM before using it. */
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_SRAM_PDN);

	/*
	 * Set I-cache and D-cache size before loading SCP FW.
	 * SCP SRAM logical address may change when cache size setting differs.
	 */
	writel(MT8183_SCP_CACHE_CON_WAYEN | MT8183_SCP_CACHESIZE_8KB,
	       scp->cluster->reg_base + MT8183_SCP_CACHE_CON);
	writel(MT8183_SCP_CACHESIZE_8KB, scp->cluster->reg_base + MT8183_SCP_DCACHE_CON);

	return 0;
}

static void scp_sram_power_on(void __iomem *addr, u32 reserved_mask)
{
	int i;

	for (i = 31; i >= 0; i--)
		writel(GENMASK(i, 0) & ~reserved_mask, addr);
	writel(0, addr);
}

static void scp_sram_power_off(void __iomem *addr, u32 reserved_mask)
{
	int i;

	writel(0, addr);
	for (i = 0; i < 32; i++)
		writel(GENMASK(i, 0) & ~reserved_mask, addr);
}

static int mt8186_scp_before_load(struct mtk_scp *scp)
{
	/* Clear SCP to host interrupt */
	writel(MT8183_SCP_IPC_INT_BIT, scp->cluster->reg_base + MT8183_SCP_TO_HOST);

	/* Reset clocks before loading FW */
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_CLK_SW_SEL);
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_CLK_DIV_SEL);

	/* Turn on the power of SCP's SRAM before using it. Enable 1 block per time*/
	scp_sram_power_on(scp->cluster->reg_base + MT8183_SCP_SRAM_PDN, 0);

	/* Initialize TCM before loading FW. */
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_L1_SRAM_PD);
	writel(0x0, scp->cluster->reg_base + MT8183_SCP_TCM_TAIL_SRAM_PD);
	writel(0x0, scp->cluster->reg_base + MT8186_SCP_L1_SRAM_PD_P1);
	writel(0x0, scp->cluster->reg_base + MT8186_SCP_L1_SRAM_PD_p2);

	/*
	 * Set I-cache and D-cache size before loading SCP FW.
	 * SCP SRAM logical address may change when cache size setting differs.
	 */
	writel(MT8183_SCP_CACHE_CON_WAYEN | MT8183_SCP_CACHESIZE_8KB,
	       scp->cluster->reg_base + MT8183_SCP_CACHE_CON);
	writel(MT8183_SCP_CACHESIZE_8KB, scp->cluster->reg_base + MT8183_SCP_DCACHE_CON);

	return 0;
}

static int mt8188_scp_l2tcm_on(struct mtk_scp *scp)
{
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;

	mutex_lock(&scp_cluster->cluster_lock);

	if (scp_cluster->l2tcm_refcnt == 0) {
		/* clear SPM interrupt, SCP2SPM_IPC_CLR */
		writel(0xff, scp->cluster->reg_base + MT8192_SCP2SPM_IPC_CLR);

		/* Power on L2TCM */
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_0, 0);
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_1, 0);
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_2, 0);
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L1TCM_SRAM_PDN, 0);
	}

	scp_cluster->l2tcm_refcnt += 1;

	mutex_unlock(&scp_cluster->cluster_lock);

	return 0;
}

static int mt8188_scp_before_load(struct mtk_scp *scp)
{
	writel(1, scp->cluster->reg_base + MT8192_CORE0_SW_RSTN_SET);

	mt8188_scp_l2tcm_on(scp);

	scp_sram_power_on(scp->cluster->reg_base + MT8192_CPU0_SRAM_PD, 0);

	/* enable MPU for all memory regions */
	writel(0xff, scp->cluster->reg_base + MT8192_CORE0_MEM_ATT_PREDEF);

	return 0;
}

static int mt8188_scp_c1_before_load(struct mtk_scp *scp)
{
	u32 sec_ctrl;
	struct mtk_scp *scp_c0;
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;

	scp->data->scp_reset_assert(scp);

	mt8188_scp_l2tcm_on(scp);

	scp_sram_power_on(scp->cluster->reg_base + MT8195_CPU1_SRAM_PD, 0);

	/* enable MPU for all memory regions */
	writel(0xff, scp->cluster->reg_base + MT8195_CORE1_MEM_ATT_PREDEF);

	/*
	 * The L2TCM_OFFSET_RANGE and L2TCM_OFFSET shift the destination address
	 * on SRAM when SCP core 1 accesses SRAM.
	 *
	 * This configuration solves booting the SCP core 0 and core 1 from
	 * different SRAM address because core 0 and core 1 both boot from
	 * the head of SRAM by default. this must be configured before boot SCP core 1.
	 *
	 * The value of L2TCM_OFFSET_RANGE is from the viewpoint of SCP core 1.
	 * When SCP core 1 issues address within the range (L2TCM_OFFSET_RANGE),
	 * the address will be added with a fixed offset (L2TCM_OFFSET) on the bus.
	 * The shift action is tranparent to software.
	 */
	writel(0, scp->cluster->reg_base + MT8195_L2TCM_OFFSET_RANGE_0_LOW);
	writel(scp->sram_size, scp->cluster->reg_base + MT8195_L2TCM_OFFSET_RANGE_0_HIGH);

	scp_c0 = list_first_entry(&scp_cluster->mtk_scp_list, struct mtk_scp, elem);
	writel(scp->sram_phys - scp_c0->sram_phys, scp->cluster->reg_base + MT8195_L2TCM_OFFSET);

	/* enable SRAM offset when fetching instruction and data */
	sec_ctrl = readl(scp->cluster->reg_base + MT8195_SEC_CTRL);
	sec_ctrl |= MT8195_CORE_OFFSET_ENABLE_I | MT8195_CORE_OFFSET_ENABLE_D;
	writel(sec_ctrl, scp->cluster->reg_base + MT8195_SEC_CTRL);

	return 0;
}

static int mt8192_scp_before_load(struct mtk_scp *scp)
{
	/* clear SPM interrupt, SCP2SPM_IPC_CLR */
	writel(0xff, scp->cluster->reg_base + MT8192_SCP2SPM_IPC_CLR);

	writel(1, scp->cluster->reg_base + MT8192_CORE0_SW_RSTN_SET);

	/* enable SRAM clock */
	scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_0, 0);
	scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_1, 0);
	scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_2, 0);
	scp_sram_power_on(scp->cluster->reg_base + MT8192_L1TCM_SRAM_PDN, 0);
	scp_sram_power_on(scp->cluster->reg_base + MT8192_CPU0_SRAM_PD, 0);

	/* enable MPU for all memory regions */
	writel(0xff, scp->cluster->reg_base + MT8192_CORE0_MEM_ATT_PREDEF);

	return 0;
}

static int mt8195_scp_l2tcm_on(struct mtk_scp *scp)
{
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;

	mutex_lock(&scp_cluster->cluster_lock);

	if (scp_cluster->l2tcm_refcnt == 0) {
		/* clear SPM interrupt, SCP2SPM_IPC_CLR */
		writel(0xff, scp->cluster->reg_base + MT8192_SCP2SPM_IPC_CLR);

		/* Power on L2TCM */
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_0, 0);
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_1, 0);
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_2, 0);
		scp_sram_power_on(scp->cluster->reg_base + MT8192_L1TCM_SRAM_PDN,
				  MT8195_L1TCM_SRAM_PDN_RESERVED_RSI_BITS);
	}

	scp_cluster->l2tcm_refcnt += 1;

	mutex_unlock(&scp_cluster->cluster_lock);

	return 0;
}

static int mt8195_scp_before_load(struct mtk_scp *scp)
{
	writel(1, scp->cluster->reg_base + MT8192_CORE0_SW_RSTN_SET);

	mt8195_scp_l2tcm_on(scp);

	scp_sram_power_on(scp->cluster->reg_base + MT8192_CPU0_SRAM_PD, 0);

	/* enable MPU for all memory regions */
	writel(0xff, scp->cluster->reg_base + MT8192_CORE0_MEM_ATT_PREDEF);

	return 0;
}

static int mt8195_scp_c1_before_load(struct mtk_scp *scp)
{
	u32 sec_ctrl;
	struct mtk_scp *scp_c0;
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;

	scp->data->scp_reset_assert(scp);

	mt8195_scp_l2tcm_on(scp);

	scp_sram_power_on(scp->cluster->reg_base + MT8195_CPU1_SRAM_PD, 0);

	/* enable MPU for all memory regions */
	writel(0xff, scp->cluster->reg_base + MT8195_CORE1_MEM_ATT_PREDEF);

	/*
	 * The L2TCM_OFFSET_RANGE and L2TCM_OFFSET shift the destination address
	 * on SRAM when SCP core 1 accesses SRAM.
	 *
	 * This configuration solves booting the SCP core 0 and core 1 from
	 * different SRAM address because core 0 and core 1 both boot from
	 * the head of SRAM by default. this must be configured before boot SCP core 1.
	 *
	 * The value of L2TCM_OFFSET_RANGE is from the viewpoint of SCP core 1.
	 * When SCP core 1 issues address within the range (L2TCM_OFFSET_RANGE),
	 * the address will be added with a fixed offset (L2TCM_OFFSET) on the bus.
	 * The shift action is tranparent to software.
	 */
	writel(0, scp->cluster->reg_base + MT8195_L2TCM_OFFSET_RANGE_0_LOW);
	writel(scp->sram_size, scp->cluster->reg_base + MT8195_L2TCM_OFFSET_RANGE_0_HIGH);

	scp_c0 = list_first_entry(&scp_cluster->mtk_scp_list, struct mtk_scp, elem);
	writel(scp->sram_phys - scp_c0->sram_phys, scp->cluster->reg_base + MT8195_L2TCM_OFFSET);

	/* enable SRAM offset when fetching instruction and data */
	sec_ctrl = readl(scp->cluster->reg_base + MT8195_SEC_CTRL);
	sec_ctrl |= MT8195_CORE_OFFSET_ENABLE_I | MT8195_CORE_OFFSET_ENABLE_D;
	writel(sec_ctrl, scp->cluster->reg_base + MT8195_SEC_CTRL);

	return 0;
}

static int scp_load(struct rproc *rproc, const struct firmware *fw)
{
	struct mtk_scp *scp = rproc->priv;
	struct device *dev = scp->dev;
	int ret;

	ret = clk_prepare_enable(scp->clk);
	if (ret) {
		dev_err(dev, "failed to enable clocks\n");
		return ret;
	}

	/* Hold SCP in reset while loading FW. */
	scp->data->scp_reset_assert(scp);

	ret = scp->data->scp_before_load(scp);
	if (ret < 0)
		goto leave;

	ret = scp_elf_load_segments(rproc, fw);
leave:
	clk_disable_unprepare(scp->clk);

	return ret;
}

static int scp_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	struct mtk_scp *scp = rproc->priv;
	struct device *dev = scp->dev;
	int ret;

	ret = clk_prepare_enable(scp->clk);
	if (ret) {
		dev_err(dev, "failed to enable clocks\n");
		return ret;
	}

	ret = scp_ipi_init(scp, fw);
	clk_disable_unprepare(scp->clk);
	return ret;
}

static int scp_start(struct rproc *rproc)
{
	struct mtk_scp *scp = rproc->priv;
	struct device *dev = scp->dev;
	struct scp_run *run = &scp->run;
	int ret;

	ret = clk_prepare_enable(scp->clk);
	if (ret) {
		dev_err(dev, "failed to enable clocks\n");
		return ret;
	}

	run->signaled = false;

	scp->data->scp_reset_deassert(scp);

	ret = wait_event_interruptible_timeout(
					run->wq,
					run->signaled,
					msecs_to_jiffies(2000));

	if (ret == 0) {
		dev_err(dev, "wait SCP initialization timeout!\n");
		ret = -ETIME;
		goto stop;
	}
	if (ret == -ERESTARTSYS) {
		dev_err(dev, "wait SCP interrupted by a signal!\n");
		goto stop;
	}

	clk_disable_unprepare(scp->clk);
	dev_info(dev, "SCP is ready. FW version %s\n", run->fw_ver);

	return 0;

stop:
	scp->data->scp_reset_assert(scp);
	clk_disable_unprepare(scp->clk);
	return ret;
}

static void *mt8183_scp_da_to_va(struct mtk_scp *scp, u64 da, size_t len)
{
	int offset;
	const struct mtk_scp_sizes_data *scp_sizes;

	scp_sizes = scp->data->scp_sizes;
	if (da < scp->sram_size) {
		offset = da;
		if (offset >= 0 && (offset + len) <= scp->sram_size)
			return (void __force *)scp->sram_base + offset;
	} else if (scp_sizes->max_dram_size) {
		offset = da - scp->dma_addr;
		if (offset >= 0 && (offset + len) <= scp_sizes->max_dram_size)
			return scp->cpu_addr + offset;
	}

	return NULL;
}

static void *mt8192_scp_da_to_va(struct mtk_scp *scp, u64 da, size_t len)
{
	int offset;
	const struct mtk_scp_sizes_data *scp_sizes;

	scp_sizes = scp->data->scp_sizes;
	if (da >= scp->sram_phys &&
	    (da + len) <= scp->sram_phys + scp->sram_size) {
		offset = da - scp->sram_phys;
		return (void __force *)scp->sram_base + offset;
	}

	/* optional memory region */
	if (scp->cluster->l1tcm_size &&
	    da >= scp->cluster->l1tcm_phys &&
	    (da + len) <= scp->cluster->l1tcm_phys + scp->cluster->l1tcm_size) {
		offset = da - scp->cluster->l1tcm_phys;
		return (void __force *)scp->cluster->l1tcm_base + offset;
	}

	/* optional memory region */
	if (scp_sizes->max_dram_size &&
	    da >= scp->dma_addr &&
	    (da + len) <= scp->dma_addr + scp_sizes->max_dram_size) {
		offset = da - scp->dma_addr;
		return scp->cpu_addr + offset;
	}

	return NULL;
}

static void *scp_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct mtk_scp *scp = rproc->priv;

	return scp->data->scp_da_to_va(scp, da, len);
}

static void mt8183_scp_stop(struct mtk_scp *scp)
{
	/* Disable SCP watchdog */
	writel(0, scp->cluster->reg_base + MT8183_WDT_CFG);
}

static void mt8188_scp_l2tcm_off(struct mtk_scp *scp)
{
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;

	mutex_lock(&scp_cluster->cluster_lock);

	if (scp_cluster->l2tcm_refcnt > 0)
		scp_cluster->l2tcm_refcnt -= 1;

	if (scp_cluster->l2tcm_refcnt == 0) {
		/* Power off L2TCM */
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_0, 0);
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_1, 0);
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_2, 0);
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L1TCM_SRAM_PDN, 0);
	}

	mutex_unlock(&scp_cluster->cluster_lock);
}

static void mt8188_scp_stop(struct mtk_scp *scp)
{
	mt8188_scp_l2tcm_off(scp);

	scp_sram_power_off(scp->cluster->reg_base + MT8192_CPU0_SRAM_PD, 0);

	/* Disable SCP watchdog */
	writel(0, scp->cluster->reg_base + MT8192_CORE0_WDT_CFG);
}

static void mt8188_scp_c1_stop(struct mtk_scp *scp)
{
	mt8188_scp_l2tcm_off(scp);

	/* Power off CPU SRAM */
	scp_sram_power_off(scp->cluster->reg_base + MT8195_CPU1_SRAM_PD, 0);

	/* Disable SCP watchdog */
	writel(0, scp->cluster->reg_base + MT8195_CORE1_WDT_CFG);
}

static void mt8192_scp_stop(struct mtk_scp *scp)
{
	/* Disable SRAM clock */
	scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_0, 0);
	scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_1, 0);
	scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_2, 0);
	scp_sram_power_off(scp->cluster->reg_base + MT8192_L1TCM_SRAM_PDN, 0);
	scp_sram_power_off(scp->cluster->reg_base + MT8192_CPU0_SRAM_PD, 0);

	/* Disable SCP watchdog */
	writel(0, scp->cluster->reg_base + MT8192_CORE0_WDT_CFG);
}

static void mt8195_scp_l2tcm_off(struct mtk_scp *scp)
{
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;

	mutex_lock(&scp_cluster->cluster_lock);

	if (scp_cluster->l2tcm_refcnt > 0)
		scp_cluster->l2tcm_refcnt -= 1;

	if (scp_cluster->l2tcm_refcnt == 0) {
		/* Power off L2TCM */
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_0, 0);
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_1, 0);
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L2TCM_SRAM_PD_2, 0);
		scp_sram_power_off(scp->cluster->reg_base + MT8192_L1TCM_SRAM_PDN,
				   MT8195_L1TCM_SRAM_PDN_RESERVED_RSI_BITS);
	}

	mutex_unlock(&scp_cluster->cluster_lock);
}

static void mt8195_scp_stop(struct mtk_scp *scp)
{
	mt8195_scp_l2tcm_off(scp);

	scp_sram_power_off(scp->cluster->reg_base + MT8192_CPU0_SRAM_PD, 0);

	/* Disable SCP watchdog */
	writel(0, scp->cluster->reg_base + MT8192_CORE0_WDT_CFG);
}

static void mt8195_scp_c1_stop(struct mtk_scp *scp)
{
	mt8195_scp_l2tcm_off(scp);

	/* Power off CPU SRAM */
	scp_sram_power_off(scp->cluster->reg_base + MT8195_CPU1_SRAM_PD, 0);

	/* Disable SCP watchdog */
	writel(0, scp->cluster->reg_base + MT8195_CORE1_WDT_CFG);
}

static int scp_stop(struct rproc *rproc)
{
	struct mtk_scp *scp = rproc->priv;
	int ret;

	ret = clk_prepare_enable(scp->clk);
	if (ret) {
		dev_err(scp->dev, "failed to enable clocks\n");
		return ret;
	}

	scp->data->scp_reset_assert(scp);
	scp->data->scp_stop(scp);
	clk_disable_unprepare(scp->clk);

	return 0;
}

static const struct rproc_ops scp_ops = {
	.start		= scp_start,
	.stop		= scp_stop,
	.load		= scp_load,
	.da_to_va	= scp_da_to_va,
	.parse_fw	= scp_parse_fw,
	.sanity_check	= rproc_elf_sanity_check,
};

/**
 * scp_get_device() - get device struct of SCP
 *
 * @scp:	mtk_scp structure
 **/
struct device *scp_get_device(struct mtk_scp *scp)
{
	return scp->dev;
}
EXPORT_SYMBOL_GPL(scp_get_device);

/**
 * scp_get_rproc() - get rproc struct of SCP
 *
 * @scp:	mtk_scp structure
 **/
struct rproc *scp_get_rproc(struct mtk_scp *scp)
{
	return scp->rproc;
}
EXPORT_SYMBOL_GPL(scp_get_rproc);

/**
 * scp_get_vdec_hw_capa() - get video decoder hardware capability
 *
 * @scp:	mtk_scp structure
 *
 * Return: video decoder hardware capability
 **/
unsigned int scp_get_vdec_hw_capa(struct mtk_scp *scp)
{
	return scp->run.dec_capability;
}
EXPORT_SYMBOL_GPL(scp_get_vdec_hw_capa);

/**
 * scp_get_venc_hw_capa() - get video encoder hardware capability
 *
 * @scp:	mtk_scp structure
 *
 * Return: video encoder hardware capability
 **/
unsigned int scp_get_venc_hw_capa(struct mtk_scp *scp)
{
	return scp->run.enc_capability;
}
EXPORT_SYMBOL_GPL(scp_get_venc_hw_capa);

/**
 * scp_mapping_dm_addr() - Mapping SRAM/DRAM to kernel virtual address
 *
 * @scp:	mtk_scp structure
 * @mem_addr:	SCP views memory address
 *
 * Mapping the SCP's SRAM address /
 * DMEM (Data Extended Memory) memory address /
 * Working buffer memory address to
 * kernel virtual address.
 *
 * Return: Return ERR_PTR(-EINVAL) if mapping failed,
 * otherwise the mapped kernel virtual address
 **/
void *scp_mapping_dm_addr(struct mtk_scp *scp, u32 mem_addr)
{
	void *ptr;

	ptr = scp_da_to_va(scp->rproc, mem_addr, 0, NULL);
	if (!ptr)
		return ERR_PTR(-EINVAL);

	return ptr;
}
EXPORT_SYMBOL_GPL(scp_mapping_dm_addr);

static int scp_map_memory_region(struct mtk_scp *scp)
{
	int ret;
	const struct mtk_scp_sizes_data *scp_sizes;

	ret = of_reserved_mem_device_init(scp->dev);

	/* reserved memory is optional. */
	if (ret == -ENODEV) {
		dev_info(scp->dev, "skipping reserved memory initialization.");
		return 0;
	}

	if (ret) {
		dev_err(scp->dev, "failed to assign memory-region: %d\n", ret);
		return -ENOMEM;
	}

	/* Reserved SCP code size */
	scp_sizes = scp->data->scp_sizes;
	scp->cpu_addr = dma_alloc_coherent(scp->dev, scp_sizes->max_dram_size,
					   &scp->dma_addr, GFP_KERNEL);
	if (!scp->cpu_addr)
		return -ENOMEM;

	return 0;
}

static void scp_unmap_memory_region(struct mtk_scp *scp)
{
	const struct mtk_scp_sizes_data *scp_sizes;

	scp_sizes = scp->data->scp_sizes;
	if (scp_sizes->max_dram_size == 0)
		return;

	dma_free_coherent(scp->dev, scp_sizes->max_dram_size, scp->cpu_addr,
			  scp->dma_addr);
	of_reserved_mem_device_release(scp->dev);
}

static int scp_register_ipi(struct platform_device *pdev, u32 id,
			    ipi_handler_t handler, void *priv)
{
	struct mtk_scp *scp = platform_get_drvdata(pdev);

	return scp_ipi_register(scp, id, handler, priv);
}

static void scp_unregister_ipi(struct platform_device *pdev, u32 id)
{
	struct mtk_scp *scp = platform_get_drvdata(pdev);

	scp_ipi_unregister(scp, id);
}

static int scp_send_ipi(struct platform_device *pdev, u32 id, void *buf,
			unsigned int len, unsigned int wait)
{
	struct mtk_scp *scp = platform_get_drvdata(pdev);

	return scp_ipi_send(scp, id, buf, len, wait);
}

static struct mtk_rpmsg_info mtk_scp_rpmsg_info = {
	.send_ipi = scp_send_ipi,
	.register_ipi = scp_register_ipi,
	.unregister_ipi = scp_unregister_ipi,
	.ns_ipi_id = SCP_IPI_NS_SERVICE,
};

static void scp_add_rpmsg_subdev(struct mtk_scp *scp)
{
	scp->rpmsg_subdev =
		mtk_rpmsg_create_rproc_subdev(to_platform_device(scp->dev),
					      &mtk_scp_rpmsg_info);
	if (scp->rpmsg_subdev)
		rproc_add_subdev(scp->rproc, scp->rpmsg_subdev);
}

static void scp_remove_rpmsg_subdev(struct mtk_scp *scp)
{
	if (scp->rpmsg_subdev) {
		rproc_remove_subdev(scp->rproc, scp->rpmsg_subdev);
		mtk_rpmsg_destroy_rproc_subdev(scp->rpmsg_subdev);
		scp->rpmsg_subdev = NULL;
	}
}

static struct mtk_scp *scp_rproc_init(struct platform_device *pdev,
				      struct mtk_scp_of_cluster *scp_cluster,
				      const struct mtk_scp_of_data *of_data)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mtk_scp *scp;
	struct rproc *rproc;
	struct resource *res;
	const char *fw_name = "scp.img";
	int ret, i;
	const struct mtk_scp_sizes_data *scp_sizes;

	ret = rproc_of_parse_firmware(dev, 0, &fw_name);
	if (ret < 0 && ret != -EINVAL)
		return ERR_PTR(ret);

	rproc = devm_rproc_alloc(dev, np->name, &scp_ops, fw_name, sizeof(*scp));
	if (!rproc) {
		dev_err(dev, "unable to allocate remoteproc\n");
		return ERR_PTR(-ENOMEM);
	}

	scp = rproc->priv;
	scp->rproc = rproc;
	scp->dev = dev;
	scp->data = of_data;
	scp->cluster = scp_cluster;
	platform_set_drvdata(pdev, scp);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	scp->sram_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(scp->sram_base)) {
		dev_err(dev, "Failed to parse and map sram memory\n");
		return ERR_CAST(scp->sram_base);
	}

	scp->sram_size = resource_size(res);
	scp->sram_phys = res->start;

	ret = scp->data->scp_clk_get(scp);
	if (ret)
		return ERR_PTR(ret);

	ret = scp_map_memory_region(scp);
	if (ret)
		return ERR_PTR(ret);

	mutex_init(&scp->send_lock);
	for (i = 0; i < SCP_IPI_MAX; i++)
		mutex_init(&scp->ipi_desc[i].lock);

	/* register SCP initialization IPI */
	ret = scp_ipi_register(scp, SCP_IPI_INIT, scp_init_ipi_handler, scp);
	if (ret) {
		dev_err(dev, "Failed to register IPI_SCP_INIT\n");
		goto release_dev_mem;
	}

	scp_sizes = scp->data->scp_sizes;
	scp->share_buf = kzalloc(scp_sizes->ipi_share_buffer_size, GFP_KERNEL);
	if (!scp->share_buf) {
		dev_err(dev, "Failed to allocate IPI share buffer\n");
		ret = -ENOMEM;
		goto release_dev_mem;
	}

	init_waitqueue_head(&scp->run.wq);
	init_waitqueue_head(&scp->ack_wq);

	scp_add_rpmsg_subdev(scp);

	ret = devm_request_threaded_irq(dev, platform_get_irq(pdev, 0), NULL,
					scp_irq_handler, IRQF_ONESHOT,
					pdev->name, scp);

	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto remove_subdev;
	}

	return scp;

remove_subdev:
	scp_remove_rpmsg_subdev(scp);
	scp_ipi_unregister(scp, SCP_IPI_INIT);
	kfree(scp->share_buf);
	scp->share_buf = NULL;
release_dev_mem:
	scp_unmap_memory_region(scp);
	for (i = 0; i < SCP_IPI_MAX; i++)
		mutex_destroy(&scp->ipi_desc[i].lock);
	mutex_destroy(&scp->send_lock);

	return ERR_PTR(ret);
}

static void scp_free(struct mtk_scp *scp)
{
	int i;

	scp_remove_rpmsg_subdev(scp);
	scp_ipi_unregister(scp, SCP_IPI_INIT);
	kfree(scp->share_buf);
	scp->share_buf = NULL;
	scp_unmap_memory_region(scp);
	for (i = 0; i < SCP_IPI_MAX; i++)
		mutex_destroy(&scp->ipi_desc[i].lock);
	mutex_destroy(&scp->send_lock);
}

static int scp_add_single_core(struct platform_device *pdev,
			       struct mtk_scp_of_cluster *scp_cluster)
{
	struct device *dev = &pdev->dev;
	struct list_head *scp_list = &scp_cluster->mtk_scp_list;
	struct mtk_scp *scp;
	int ret;

	scp = scp_rproc_init(pdev, scp_cluster, of_device_get_match_data(dev));
	if (IS_ERR(scp))
		return PTR_ERR(scp);

	ret = rproc_add(scp->rproc);
	if (ret) {
		dev_err(dev, "Failed to add rproc\n");
		scp_free(scp);
		return ret;
	}

	list_add_tail(&scp->elem, scp_list);

	return 0;
}

static int scp_add_multi_core(struct platform_device *pdev,
			      struct mtk_scp_of_cluster *scp_cluster)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct platform_device *cpdev;
	struct device_node *child;
	struct list_head *scp_list = &scp_cluster->mtk_scp_list;
	const struct mtk_scp_of_data **cluster_of_data;
	struct mtk_scp *scp, *temp;
	int core_id = 0;
	int ret;

	cluster_of_data = (const struct mtk_scp_of_data **)of_device_get_match_data(dev);

	for_each_available_child_of_node(np, child) {
		if (!cluster_of_data[core_id]) {
			ret = -EINVAL;
			dev_err(dev, "Not support core %d\n", core_id);
			of_node_put(child);
			goto init_fail;
		}

		cpdev = of_find_device_by_node(child);
		if (!cpdev) {
			ret = -ENODEV;
			dev_err(dev, "Not found platform device for core %d\n", core_id);
			of_node_put(child);
			goto init_fail;
		}

		scp = scp_rproc_init(cpdev, scp_cluster, cluster_of_data[core_id]);
		put_device(&cpdev->dev);
		if (IS_ERR(scp)) {
			ret = PTR_ERR(scp);
			dev_err(dev, "Failed to initialize core %d rproc\n", core_id);
			of_node_put(child);
			goto init_fail;
		}

		ret = rproc_add(scp->rproc);
		if (ret) {
			dev_err(dev, "Failed to add rproc of core %d\n", core_id);
			of_node_put(child);
			scp_free(scp);
			goto init_fail;
		}

		list_add_tail(&scp->elem, scp_list);
		core_id++;
	}

	/*
	 * Here we are setting the platform device for @pdev to the last @scp that was
	 * created, which is needed because (1) scp_rproc_init() is calling
	 * platform_set_drvdata() on the child platform devices and (2) we need a handle to
	 * the cluster list in scp_remove().
	 */
	platform_set_drvdata(pdev, scp);

	return 0;

init_fail:
	list_for_each_entry_safe_reverse(scp, temp, scp_list, elem) {
		list_del(&scp->elem);
		rproc_del(scp->rproc);
		scp_free(scp);
	}

	return ret;
}

static bool scp_is_single_core(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct device_node *child;
	int num_cores = 0;

	for_each_child_of_node(np, child)
		if (of_device_is_compatible(child, "mediatek,scp-core"))
			num_cores++;

	return num_cores < 2;
}

static int scp_cluster_init(struct platform_device *pdev, struct mtk_scp_of_cluster *scp_cluster)
{
	int ret;

	if (scp_is_single_core(pdev))
		ret = scp_add_single_core(pdev, scp_cluster);
	else
		ret = scp_add_multi_core(pdev, scp_cluster);

	return ret;
}

static int scp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_scp_of_cluster *scp_cluster;
	struct resource *res;
	int ret;

	scp_cluster = devm_kzalloc(dev, sizeof(*scp_cluster), GFP_KERNEL);
	if (!scp_cluster)
		return -ENOMEM;

	scp_cluster->reg_base = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(scp_cluster->reg_base))
		return dev_err_probe(dev, PTR_ERR(scp_cluster->reg_base),
				     "Failed to parse and map cfg memory\n");

	/* l1tcm is an optional memory region */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "l1tcm");
	if (res) {
		scp_cluster->l1tcm_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(scp_cluster->l1tcm_base))
			return dev_err_probe(dev, PTR_ERR(scp_cluster->l1tcm_base),
					     "Failed to map l1tcm memory\n");

		scp_cluster->l1tcm_size = resource_size(res);
		scp_cluster->l1tcm_phys = res->start;
	}

	INIT_LIST_HEAD(&scp_cluster->mtk_scp_list);
	mutex_init(&scp_cluster->cluster_lock);

	ret = devm_of_platform_populate(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to populate platform devices\n");

	ret = scp_cluster_init(pdev, scp_cluster);
	if (ret)
		return ret;

	return 0;
}

static void scp_remove(struct platform_device *pdev)
{
	struct mtk_scp *scp = platform_get_drvdata(pdev);
	struct mtk_scp_of_cluster *scp_cluster = scp->cluster;
	struct mtk_scp *temp;

	list_for_each_entry_safe_reverse(scp, temp, &scp_cluster->mtk_scp_list, elem) {
		list_del(&scp->elem);
		rproc_del(scp->rproc);
		scp_free(scp);
	}
	mutex_destroy(&scp_cluster->cluster_lock);
}

static const struct mtk_scp_sizes_data default_scp_sizes = {
	.max_dram_size = 0x500000,
	.ipi_share_buffer_size = 288,
};

static const struct mtk_scp_sizes_data mt8188_scp_sizes = {
	.max_dram_size = 0x800000,
	.ipi_share_buffer_size = 600,
};

static const struct mtk_scp_sizes_data mt8188_scp_c1_sizes = {
	.max_dram_size = 0xA00000,
	.ipi_share_buffer_size = 600,
};

static const struct mtk_scp_sizes_data mt8195_scp_sizes = {
	.max_dram_size = 0x800000,
	.ipi_share_buffer_size = 288,
};

static const struct mtk_scp_of_data mt8183_of_data = {
	.scp_clk_get = mt8183_scp_clk_get,
	.scp_before_load = mt8183_scp_before_load,
	.scp_irq_handler = mt8183_scp_irq_handler,
	.scp_reset_assert = mt8183_scp_reset_assert,
	.scp_reset_deassert = mt8183_scp_reset_deassert,
	.scp_stop = mt8183_scp_stop,
	.scp_da_to_va = mt8183_scp_da_to_va,
	.host_to_scp_reg = MT8183_HOST_TO_SCP,
	.host_to_scp_int_bit = MT8183_HOST_IPC_INT_BIT,
	.ipi_buf_offset = 0x7bdb0,
	.scp_sizes = &default_scp_sizes,
};

static const struct mtk_scp_of_data mt8186_of_data = {
	.scp_clk_get = mt8195_scp_clk_get,
	.scp_before_load = mt8186_scp_before_load,
	.scp_irq_handler = mt8183_scp_irq_handler,
	.scp_reset_assert = mt8183_scp_reset_assert,
	.scp_reset_deassert = mt8183_scp_reset_deassert,
	.scp_stop = mt8183_scp_stop,
	.scp_da_to_va = mt8183_scp_da_to_va,
	.host_to_scp_reg = MT8183_HOST_TO_SCP,
	.host_to_scp_int_bit = MT8183_HOST_IPC_INT_BIT,
	.ipi_buf_offset = 0x3bdb0,
	.scp_sizes = &default_scp_sizes,
};

static const struct mtk_scp_of_data mt8188_of_data = {
	.scp_clk_get = mt8195_scp_clk_get,
	.scp_before_load = mt8188_scp_before_load,
	.scp_irq_handler = mt8195_scp_irq_handler,
	.scp_reset_assert = mt8192_scp_reset_assert,
	.scp_reset_deassert = mt8192_scp_reset_deassert,
	.scp_stop = mt8188_scp_stop,
	.scp_da_to_va = mt8192_scp_da_to_va,
	.host_to_scp_reg = MT8192_GIPC_IN_SET,
	.host_to_scp_int_bit = MT8192_HOST_IPC_INT_BIT,
	.scp_sizes = &mt8188_scp_sizes,
};

static const struct mtk_scp_of_data mt8188_of_data_c1 = {
	.scp_clk_get = mt8195_scp_clk_get,
	.scp_before_load = mt8188_scp_c1_before_load,
	.scp_irq_handler = mt8195_scp_c1_irq_handler,
	.scp_reset_assert = mt8195_scp_c1_reset_assert,
	.scp_reset_deassert = mt8195_scp_c1_reset_deassert,
	.scp_stop = mt8188_scp_c1_stop,
	.scp_da_to_va = mt8192_scp_da_to_va,
	.host_to_scp_reg = MT8192_GIPC_IN_SET,
	.host_to_scp_int_bit = MT8195_CORE1_HOST_IPC_INT_BIT,
	.scp_sizes = &mt8188_scp_c1_sizes,
};

static const struct mtk_scp_of_data mt8192_of_data = {
	.scp_clk_get = mt8192_scp_clk_get,
	.scp_before_load = mt8192_scp_before_load,
	.scp_irq_handler = mt8192_scp_irq_handler,
	.scp_reset_assert = mt8192_scp_reset_assert,
	.scp_reset_deassert = mt8192_scp_reset_deassert,
	.scp_stop = mt8192_scp_stop,
	.scp_da_to_va = mt8192_scp_da_to_va,
	.host_to_scp_reg = MT8192_GIPC_IN_SET,
	.host_to_scp_int_bit = MT8192_HOST_IPC_INT_BIT,
	.scp_sizes = &default_scp_sizes,
};

static const struct mtk_scp_of_data mt8195_of_data = {
	.scp_clk_get = mt8195_scp_clk_get,
	.scp_before_load = mt8195_scp_before_load,
	.scp_irq_handler = mt8195_scp_irq_handler,
	.scp_reset_assert = mt8192_scp_reset_assert,
	.scp_reset_deassert = mt8192_scp_reset_deassert,
	.scp_stop = mt8195_scp_stop,
	.scp_da_to_va = mt8192_scp_da_to_va,
	.host_to_scp_reg = MT8192_GIPC_IN_SET,
	.host_to_scp_int_bit = MT8192_HOST_IPC_INT_BIT,
	.scp_sizes = &mt8195_scp_sizes,
};

static const struct mtk_scp_of_data mt8195_of_data_c1 = {
	.scp_clk_get = mt8195_scp_clk_get,
	.scp_before_load = mt8195_scp_c1_before_load,
	.scp_irq_handler = mt8195_scp_c1_irq_handler,
	.scp_reset_assert = mt8195_scp_c1_reset_assert,
	.scp_reset_deassert = mt8195_scp_c1_reset_deassert,
	.scp_stop = mt8195_scp_c1_stop,
	.scp_da_to_va = mt8192_scp_da_to_va,
	.host_to_scp_reg = MT8192_GIPC_IN_SET,
	.host_to_scp_int_bit = MT8195_CORE1_HOST_IPC_INT_BIT,
	.scp_sizes = &default_scp_sizes,
};

static const struct mtk_scp_of_data *mt8188_of_data_cores[] = {
	&mt8188_of_data,
	&mt8188_of_data_c1,
	NULL
};

static const struct mtk_scp_of_data *mt8195_of_data_cores[] = {
	&mt8195_of_data,
	&mt8195_of_data_c1,
	NULL
};

static const struct of_device_id mtk_scp_of_match[] = {
	{ .compatible = "mediatek,mt8183-scp", .data = &mt8183_of_data },
	{ .compatible = "mediatek,mt8186-scp", .data = &mt8186_of_data },
	{ .compatible = "mediatek,mt8188-scp", .data = &mt8188_of_data },
	{ .compatible = "mediatek,mt8188-scp-dual", .data = &mt8188_of_data_cores },
	{ .compatible = "mediatek,mt8192-scp", .data = &mt8192_of_data },
	{ .compatible = "mediatek,mt8195-scp", .data = &mt8195_of_data },
	{ .compatible = "mediatek,mt8195-scp-dual", .data = &mt8195_of_data_cores },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_scp_of_match);

static struct platform_driver mtk_scp_driver = {
	.probe = scp_probe,
	.remove_new = scp_remove,
	.driver = {
		.name = "mtk-scp",
		.of_match_table = mtk_scp_of_match,
	},
};

module_platform_driver(mtk_scp_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek SCP control driver");

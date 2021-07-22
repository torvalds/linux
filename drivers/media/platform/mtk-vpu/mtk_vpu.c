// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: Andrew-CT Chen <andrew-ct.chen@mediatek.com>
*/
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/dma-mapping.h>

#include "mtk_vpu.h"

/*
 * VPU (video processor unit) is a tiny processor controlling video hardware
 * related to video codec, scaling and color format converting.
 * VPU interfaces with other blocks by share memory and interrupt.
 */

#define INIT_TIMEOUT_MS		2000U
#define IPI_TIMEOUT_MS		2000U
#define VPU_IDLE_TIMEOUT_MS	1000U
#define VPU_FW_VER_LEN		16

/* maximum program/data TCM (Tightly-Coupled Memory) size */
#define VPU_PTCM_SIZE		(96 * SZ_1K)
#define VPU_DTCM_SIZE		(32 * SZ_1K)
/* the offset to get data tcm address */
#define VPU_DTCM_OFFSET		0x18000UL
/* daynamic allocated maximum extended memory size */
#define VPU_EXT_P_SIZE		SZ_1M
#define VPU_EXT_D_SIZE		SZ_4M
/* maximum binary firmware size */
#define VPU_P_FW_SIZE		(VPU_PTCM_SIZE + VPU_EXT_P_SIZE)
#define VPU_D_FW_SIZE		(VPU_DTCM_SIZE + VPU_EXT_D_SIZE)
/* the size of share buffer between Host and  VPU */
#define SHARE_BUF_SIZE		48

/* binary firmware name */
#define VPU_P_FW		"vpu_p.bin"
#define VPU_D_FW		"vpu_d.bin"
#define VPU_P_FW_NEW		"mediatek/mt8173/vpu_p.bin"
#define VPU_D_FW_NEW		"mediatek/mt8173/vpu_d.bin"

#define VPU_RESET		0x0
#define VPU_TCM_CFG		0x0008
#define VPU_PMEM_EXT0_ADDR	0x000C
#define VPU_PMEM_EXT1_ADDR	0x0010
#define VPU_TO_HOST		0x001C
#define VPU_DMEM_EXT0_ADDR	0x0014
#define VPU_DMEM_EXT1_ADDR	0x0018
#define HOST_TO_VPU		0x0024
#define VPU_IDLE_REG		0x002C
#define VPU_INT_STATUS		0x0034
#define VPU_PC_REG		0x0060
#define VPU_SP_REG		0x0064
#define VPU_RA_REG		0x0068
#define VPU_WDT_REG		0x0084

/* vpu inter-processor communication interrupt */
#define VPU_IPC_INT		BIT(8)
/* vpu idle state */
#define VPU_IDLE_STATE		BIT(23)

/**
 * enum vpu_fw_type - VPU firmware type
 *
 * @P_FW: program firmware
 * @D_FW: data firmware
 *
 */
enum vpu_fw_type {
	P_FW,
	D_FW,
};

/**
 * struct vpu_mem - VPU extended program/data memory information
 *
 * @va:		the kernel virtual memory address of VPU extended memory
 * @pa:		the physical memory address of VPU extended memory
 *
 */
struct vpu_mem {
	void *va;
	dma_addr_t pa;
};

/**
 * struct vpu_regs - VPU TCM and configuration registers
 *
 * @tcm:	the register for VPU Tightly-Coupled Memory
 * @cfg:	the register for VPU configuration
 * @irq:	the irq number for VPU interrupt
 */
struct vpu_regs {
	void __iomem *tcm;
	void __iomem *cfg;
	int irq;
};

/**
 * struct vpu_wdt_handler - VPU watchdog reset handler
 *
 * @reset_func:	reset handler
 * @priv:	private data
 */
struct vpu_wdt_handler {
	void (*reset_func)(void *);
	void *priv;
};

/**
 * struct vpu_wdt - VPU watchdog workqueue
 *
 * @handler:	VPU watchdog reset handler
 * @ws:		workstruct for VPU watchdog
 * @wq:		workqueue for VPU watchdog
 */
struct vpu_wdt {
	struct vpu_wdt_handler handler[VPU_RST_MAX];
	struct work_struct ws;
	struct workqueue_struct *wq;
};

/**
 * struct vpu_run - VPU initialization status
 *
 * @signaled:		the signal of vpu initialization completed
 * @fw_ver:		VPU firmware version
 * @dec_capability:	decoder capability which is not used for now and
 *			the value is reserved for future use
 * @enc_capability:	encoder capability which is not used for now and
 *			the value is reserved for future use
 * @wq:			wait queue for VPU initialization status
 */
struct vpu_run {
	u32 signaled;
	char fw_ver[VPU_FW_VER_LEN];
	unsigned int	dec_capability;
	unsigned int	enc_capability;
	wait_queue_head_t wq;
};

/**
 * struct vpu_ipi_desc - VPU IPI descriptor
 *
 * @handler:	IPI handler
 * @name:	the name of IPI handler
 * @priv:	the private data of IPI handler
 */
struct vpu_ipi_desc {
	ipi_handler_t handler;
	const char *name;
	void *priv;
};

/**
 * struct share_obj - DTCM (Data Tightly-Coupled Memory) buffer shared with
 *		      AP and VPU
 *
 * @id:		IPI id
 * @len:	share buffer length
 * @share_buf:	share buffer data
 */
struct share_obj {
	s32 id;
	u32 len;
	unsigned char share_buf[SHARE_BUF_SIZE];
};

/**
 * struct mtk_vpu - vpu driver data
 * @extmem:		VPU extended memory information
 * @reg:		VPU TCM and configuration registers
 * @run:		VPU initialization status
 * @wdt:		VPU watchdog workqueue
 * @ipi_desc:		VPU IPI descriptor
 * @recv_buf:		VPU DTCM share buffer for receiving. The
 *			receive buffer is only accessed in interrupt context.
 * @send_buf:		VPU DTCM share buffer for sending
 * @dev:		VPU struct device
 * @clk:		VPU clock on/off
 * @fw_loaded:		indicate VPU firmware loaded
 * @enable_4GB:		VPU 4GB mode on/off
 * @vpu_mutex:		protect mtk_vpu (except recv_buf) and ensure only
 *			one client to use VPU service at a time. For example,
 *			suppose a client is using VPU to decode VP8.
 *			If the other client wants to encode VP8,
 *			it has to wait until VP8 decode completes.
 * @wdt_refcnt:		WDT reference count to make sure the watchdog can be
 *			disabled if no other client is using VPU service
 * @ack_wq:		The wait queue for each codec and mdp. When sleeping
 *			processes wake up, they will check the condition
 *			"ipi_id_ack" to run the corresponding action or
 *			go back to sleep.
 * @ipi_id_ack:		The ACKs for registered IPI function sending
 *			interrupt to VPU
 *
 */
struct mtk_vpu {
	struct vpu_mem extmem[2];
	struct vpu_regs reg;
	struct vpu_run run;
	struct vpu_wdt wdt;
	struct vpu_ipi_desc ipi_desc[IPI_MAX];
	struct share_obj __iomem *recv_buf;
	struct share_obj __iomem *send_buf;
	struct device *dev;
	struct clk *clk;
	bool fw_loaded;
	bool enable_4GB;
	struct mutex vpu_mutex; /* for protecting vpu data data structure */
	u32 wdt_refcnt;
	wait_queue_head_t ack_wq;
	bool ipi_id_ack[IPI_MAX];
};

static inline void vpu_cfg_writel(struct mtk_vpu *vpu, u32 val, u32 offset)
{
	writel(val, vpu->reg.cfg + offset);
}

static inline u32 vpu_cfg_readl(struct mtk_vpu *vpu, u32 offset)
{
	return readl(vpu->reg.cfg + offset);
}

static inline bool vpu_running(struct mtk_vpu *vpu)
{
	return vpu_cfg_readl(vpu, VPU_RESET) & BIT(0);
}

static void vpu_clock_disable(struct mtk_vpu *vpu)
{
	/* Disable VPU watchdog */
	mutex_lock(&vpu->vpu_mutex);
	if (!--vpu->wdt_refcnt)
		vpu_cfg_writel(vpu,
			       vpu_cfg_readl(vpu, VPU_WDT_REG) & ~(1L << 31),
			       VPU_WDT_REG);
	mutex_unlock(&vpu->vpu_mutex);

	clk_disable(vpu->clk);
}

static int vpu_clock_enable(struct mtk_vpu *vpu)
{
	int ret;

	ret = clk_enable(vpu->clk);
	if (ret)
		return ret;
	/* Enable VPU watchdog */
	mutex_lock(&vpu->vpu_mutex);
	if (!vpu->wdt_refcnt++)
		vpu_cfg_writel(vpu,
			       vpu_cfg_readl(vpu, VPU_WDT_REG) | (1L << 31),
			       VPU_WDT_REG);
	mutex_unlock(&vpu->vpu_mutex);

	return ret;
}

static void vpu_dump_status(struct mtk_vpu *vpu)
{
	dev_info(vpu->dev,
		 "vpu: run %x, pc = 0x%x, ra = 0x%x, sp = 0x%x, idle = 0x%x\n"
		 "vpu: int %x, hv = 0x%x, vh = 0x%x, wdt = 0x%x\n",
		 vpu_running(vpu), vpu_cfg_readl(vpu, VPU_PC_REG),
		 vpu_cfg_readl(vpu, VPU_RA_REG), vpu_cfg_readl(vpu, VPU_SP_REG),
		 vpu_cfg_readl(vpu, VPU_IDLE_REG),
		 vpu_cfg_readl(vpu, VPU_INT_STATUS),
		 vpu_cfg_readl(vpu, HOST_TO_VPU),
		 vpu_cfg_readl(vpu, VPU_TO_HOST),
		 vpu_cfg_readl(vpu, VPU_WDT_REG));
}

int vpu_ipi_register(struct platform_device *pdev,
		     enum ipi_id id, ipi_handler_t handler,
		     const char *name, void *priv)
{
	struct mtk_vpu *vpu = platform_get_drvdata(pdev);
	struct vpu_ipi_desc *ipi_desc;

	if (!vpu) {
		dev_err(&pdev->dev, "vpu device in not ready\n");
		return -EPROBE_DEFER;
	}

	if (id < IPI_MAX && handler) {
		ipi_desc = vpu->ipi_desc;
		ipi_desc[id].name = name;
		ipi_desc[id].handler = handler;
		ipi_desc[id].priv = priv;
		return 0;
	}

	dev_err(&pdev->dev, "register vpu ipi id %d with invalid arguments\n",
		id);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(vpu_ipi_register);

int vpu_ipi_send(struct platform_device *pdev,
		 enum ipi_id id, void *buf,
		 unsigned int len)
{
	struct mtk_vpu *vpu = platform_get_drvdata(pdev);
	struct share_obj __iomem *send_obj = vpu->send_buf;
	unsigned long timeout;
	int ret = 0;

	if (id <= IPI_VPU_INIT || id >= IPI_MAX ||
	    len > sizeof(send_obj->share_buf) || !buf) {
		dev_err(vpu->dev, "failed to send ipi message\n");
		return -EINVAL;
	}

	ret = vpu_clock_enable(vpu);
	if (ret) {
		dev_err(vpu->dev, "failed to enable vpu clock\n");
		return ret;
	}
	if (!vpu_running(vpu)) {
		dev_err(vpu->dev, "vpu_ipi_send: VPU is not running\n");
		ret = -EINVAL;
		goto clock_disable;
	}

	mutex_lock(&vpu->vpu_mutex);

	 /* Wait until VPU receives the last command */
	timeout = jiffies + msecs_to_jiffies(IPI_TIMEOUT_MS);
	do {
		if (time_after(jiffies, timeout)) {
			dev_err(vpu->dev, "vpu_ipi_send: IPI timeout!\n");
			ret = -EIO;
			vpu_dump_status(vpu);
			goto mut_unlock;
		}
	} while (vpu_cfg_readl(vpu, HOST_TO_VPU));

	memcpy_toio(send_obj->share_buf, buf, len);
	writel(len, &send_obj->len);
	writel(id, &send_obj->id);

	vpu->ipi_id_ack[id] = false;
	/* send the command to VPU */
	vpu_cfg_writel(vpu, 0x1, HOST_TO_VPU);

	mutex_unlock(&vpu->vpu_mutex);

	/* wait for VPU's ACK */
	timeout = msecs_to_jiffies(IPI_TIMEOUT_MS);
	ret = wait_event_timeout(vpu->ack_wq, vpu->ipi_id_ack[id], timeout);
	vpu->ipi_id_ack[id] = false;
	if (ret == 0) {
		dev_err(vpu->dev, "vpu ipi %d ack time out !\n", id);
		ret = -EIO;
		vpu_dump_status(vpu);
		goto clock_disable;
	}
	vpu_clock_disable(vpu);

	return 0;

mut_unlock:
	mutex_unlock(&vpu->vpu_mutex);
clock_disable:
	vpu_clock_disable(vpu);

	return ret;
}
EXPORT_SYMBOL_GPL(vpu_ipi_send);

static void vpu_wdt_reset_func(struct work_struct *ws)
{
	struct vpu_wdt *wdt = container_of(ws, struct vpu_wdt, ws);
	struct mtk_vpu *vpu = container_of(wdt, struct mtk_vpu, wdt);
	struct vpu_wdt_handler *handler = wdt->handler;
	int index, ret;

	dev_info(vpu->dev, "vpu reset\n");
	ret = vpu_clock_enable(vpu);
	if (ret) {
		dev_err(vpu->dev, "[VPU] wdt enables clock failed %d\n", ret);
		return;
	}
	mutex_lock(&vpu->vpu_mutex);
	vpu_cfg_writel(vpu, 0x0, VPU_RESET);
	vpu->fw_loaded = false;
	mutex_unlock(&vpu->vpu_mutex);
	vpu_clock_disable(vpu);

	for (index = 0; index < VPU_RST_MAX; index++) {
		if (handler[index].reset_func) {
			handler[index].reset_func(handler[index].priv);
			dev_dbg(vpu->dev, "wdt handler func %d\n", index);
		}
	}
}

int vpu_wdt_reg_handler(struct platform_device *pdev,
			void wdt_reset(void *),
			void *priv, enum rst_id id)
{
	struct mtk_vpu *vpu = platform_get_drvdata(pdev);
	struct vpu_wdt_handler *handler;

	if (!vpu) {
		dev_err(&pdev->dev, "vpu device in not ready\n");
		return -EPROBE_DEFER;
	}

	handler = vpu->wdt.handler;

	if (id < VPU_RST_MAX && wdt_reset) {
		dev_dbg(vpu->dev, "wdt register id %d\n", id);
		mutex_lock(&vpu->vpu_mutex);
		handler[id].reset_func = wdt_reset;
		handler[id].priv = priv;
		mutex_unlock(&vpu->vpu_mutex);
		return 0;
	}

	dev_err(vpu->dev, "register vpu wdt handler failed\n");
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(vpu_wdt_reg_handler);

unsigned int vpu_get_vdec_hw_capa(struct platform_device *pdev)
{
	struct mtk_vpu *vpu = platform_get_drvdata(pdev);

	return vpu->run.dec_capability;
}
EXPORT_SYMBOL_GPL(vpu_get_vdec_hw_capa);

unsigned int vpu_get_venc_hw_capa(struct platform_device *pdev)
{
	struct mtk_vpu *vpu = platform_get_drvdata(pdev);

	return vpu->run.enc_capability;
}
EXPORT_SYMBOL_GPL(vpu_get_venc_hw_capa);

void *vpu_mapping_dm_addr(struct platform_device *pdev,
			  u32 dtcm_dmem_addr)
{
	struct mtk_vpu *vpu = platform_get_drvdata(pdev);

	if (!dtcm_dmem_addr ||
	    (dtcm_dmem_addr > (VPU_DTCM_SIZE + VPU_EXT_D_SIZE))) {
		dev_err(vpu->dev, "invalid virtual data memory address\n");
		return ERR_PTR(-EINVAL);
	}

	if (dtcm_dmem_addr < VPU_DTCM_SIZE)
		return (__force void *)(dtcm_dmem_addr + vpu->reg.tcm +
					VPU_DTCM_OFFSET);

	return vpu->extmem[D_FW].va + (dtcm_dmem_addr - VPU_DTCM_SIZE);
}
EXPORT_SYMBOL_GPL(vpu_mapping_dm_addr);

struct platform_device *vpu_get_plat_device(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *vpu_node;
	struct platform_device *vpu_pdev;

	vpu_node = of_parse_phandle(dev->of_node, "mediatek,vpu", 0);
	if (!vpu_node) {
		dev_err(dev, "can't get vpu node\n");
		return NULL;
	}

	vpu_pdev = of_find_device_by_node(vpu_node);
	of_node_put(vpu_node);
	if (WARN_ON(!vpu_pdev)) {
		dev_err(dev, "vpu pdev failed\n");
		return NULL;
	}

	return vpu_pdev;
}
EXPORT_SYMBOL_GPL(vpu_get_plat_device);

/* load vpu program/data memory */
static int load_requested_vpu(struct mtk_vpu *vpu,
			      u8 fw_type)
{
	size_t tcm_size = fw_type ? VPU_DTCM_SIZE : VPU_PTCM_SIZE;
	size_t fw_size = fw_type ? VPU_D_FW_SIZE : VPU_P_FW_SIZE;
	char *fw_name = fw_type ? VPU_D_FW : VPU_P_FW;
	char *fw_new_name = fw_type ? VPU_D_FW_NEW : VPU_P_FW_NEW;
	const struct firmware *vpu_fw;
	size_t dl_size = 0;
	size_t extra_fw_size = 0;
	void *dest;
	int ret;

	ret = request_firmware(&vpu_fw, fw_new_name, vpu->dev);
	if (ret < 0) {
		dev_info(vpu->dev, "Failed to load %s, %d, retry\n",
			 fw_new_name, ret);

		ret = request_firmware(&vpu_fw, fw_name, vpu->dev);
		if (ret < 0) {
			dev_err(vpu->dev, "Failed to load %s, %d\n", fw_name,
				ret);
			return ret;
		}
	}
	dl_size = vpu_fw->size;
	if (dl_size > fw_size) {
		dev_err(vpu->dev, "fw %s size %zu is abnormal\n", fw_name,
			dl_size);
		release_firmware(vpu_fw);
		return  -EFBIG;
	}
	dev_dbg(vpu->dev, "Downloaded fw %s size: %zu.\n",
		fw_name,
		dl_size);
	/* reset VPU */
	vpu_cfg_writel(vpu, 0x0, VPU_RESET);

	/* handle extended firmware size */
	if (dl_size > tcm_size) {
		dev_dbg(vpu->dev, "fw size %zu > limited fw size %zu\n",
			dl_size, tcm_size);
		extra_fw_size = dl_size - tcm_size;
		dev_dbg(vpu->dev, "extra_fw_size %zu\n", extra_fw_size);
		dl_size = tcm_size;
	}
	dest = (__force void *)vpu->reg.tcm;
	if (fw_type == D_FW)
		dest += VPU_DTCM_OFFSET;
	memcpy(dest, vpu_fw->data, dl_size);
	/* download to extended memory if need */
	if (extra_fw_size > 0) {
		dest = vpu->extmem[fw_type].va;
		dev_dbg(vpu->dev, "download extended memory type %x\n",
			fw_type);
		memcpy(dest, vpu_fw->data + tcm_size, extra_fw_size);
	}

	release_firmware(vpu_fw);

	return 0;
}

int vpu_load_firmware(struct platform_device *pdev)
{
	struct mtk_vpu *vpu;
	struct device *dev = &pdev->dev;
	struct vpu_run *run;
	int ret;

	if (!pdev) {
		dev_err(dev, "VPU platform device is invalid\n");
		return -EINVAL;
	}

	vpu = platform_get_drvdata(pdev);
	run = &vpu->run;

	mutex_lock(&vpu->vpu_mutex);
	if (vpu->fw_loaded) {
		mutex_unlock(&vpu->vpu_mutex);
		return 0;
	}
	mutex_unlock(&vpu->vpu_mutex);

	ret = vpu_clock_enable(vpu);
	if (ret) {
		dev_err(dev, "enable clock failed %d\n", ret);
		return ret;
	}

	mutex_lock(&vpu->vpu_mutex);

	run->signaled = false;
	dev_dbg(vpu->dev, "firmware request\n");
	/* Downloading program firmware to device*/
	ret = load_requested_vpu(vpu, P_FW);
	if (ret < 0) {
		dev_err(dev, "Failed to request %s, %d\n", VPU_P_FW, ret);
		goto OUT_LOAD_FW;
	}

	/* Downloading data firmware to device */
	ret = load_requested_vpu(vpu, D_FW);
	if (ret < 0) {
		dev_err(dev, "Failed to request %s, %d\n", VPU_D_FW, ret);
		goto OUT_LOAD_FW;
	}

	vpu->fw_loaded = true;
	/* boot up vpu */
	vpu_cfg_writel(vpu, 0x1, VPU_RESET);

	ret = wait_event_interruptible_timeout(run->wq,
					       run->signaled,
					       msecs_to_jiffies(INIT_TIMEOUT_MS)
					       );
	if (ret == 0) {
		ret = -ETIME;
		dev_err(dev, "wait vpu initialization timeout!\n");
		goto OUT_LOAD_FW;
	} else if (-ERESTARTSYS == ret) {
		dev_err(dev, "wait vpu interrupted by a signal!\n");
		goto OUT_LOAD_FW;
	}

	ret = 0;
	dev_info(dev, "vpu is ready. Fw version %s\n", run->fw_ver);

OUT_LOAD_FW:
	mutex_unlock(&vpu->vpu_mutex);
	vpu_clock_disable(vpu);

	return ret;
}
EXPORT_SYMBOL_GPL(vpu_load_firmware);

static void vpu_init_ipi_handler(const void *data, unsigned int len, void *priv)
{
	struct mtk_vpu *vpu = priv;
	const struct vpu_run *run = data;

	vpu->run.signaled = run->signaled;
	strscpy(vpu->run.fw_ver, run->fw_ver, sizeof(vpu->run.fw_ver));
	vpu->run.dec_capability = run->dec_capability;
	vpu->run.enc_capability = run->enc_capability;
	wake_up_interruptible(&vpu->run.wq);
}

#ifdef CONFIG_DEBUG_FS
static ssize_t vpu_debug_read(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	char buf[256];
	unsigned int len;
	unsigned int running, pc, vpu_to_host, host_to_vpu, wdt, idle, ra, sp;
	int ret;
	struct device *dev = file->private_data;
	struct mtk_vpu *vpu = dev_get_drvdata(dev);

	ret = vpu_clock_enable(vpu);
	if (ret) {
		dev_err(vpu->dev, "[VPU] enable clock failed %d\n", ret);
		return 0;
	}

	/* vpu register status */
	running = vpu_running(vpu);
	pc = vpu_cfg_readl(vpu, VPU_PC_REG);
	wdt = vpu_cfg_readl(vpu, VPU_WDT_REG);
	host_to_vpu = vpu_cfg_readl(vpu, HOST_TO_VPU);
	vpu_to_host = vpu_cfg_readl(vpu, VPU_TO_HOST);
	ra = vpu_cfg_readl(vpu, VPU_RA_REG);
	sp = vpu_cfg_readl(vpu, VPU_SP_REG);
	idle = vpu_cfg_readl(vpu, VPU_IDLE_REG);

	vpu_clock_disable(vpu);

	if (running) {
		len = snprintf(buf, sizeof(buf), "VPU is running\n\n"
		"FW Version: %s\n"
		"PC: 0x%x\n"
		"WDT: 0x%x\n"
		"Host to VPU: 0x%x\n"
		"VPU to Host: 0x%x\n"
		"SP: 0x%x\n"
		"RA: 0x%x\n"
		"idle: 0x%x\n",
		vpu->run.fw_ver, pc, wdt,
		host_to_vpu, vpu_to_host, sp, ra, idle);
	} else {
		len = snprintf(buf, sizeof(buf), "VPU not running\n");
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations vpu_debug_fops = {
	.open = simple_open,
	.read = vpu_debug_read,
};
#endif /* CONFIG_DEBUG_FS */

static void vpu_free_ext_mem(struct mtk_vpu *vpu, u8 fw_type)
{
	struct device *dev = vpu->dev;
	size_t fw_ext_size = fw_type ? VPU_EXT_D_SIZE : VPU_EXT_P_SIZE;

	dma_free_coherent(dev, fw_ext_size, vpu->extmem[fw_type].va,
			  vpu->extmem[fw_type].pa);
}

static int vpu_alloc_ext_mem(struct mtk_vpu *vpu, u32 fw_type)
{
	struct device *dev = vpu->dev;
	size_t fw_ext_size = fw_type ? VPU_EXT_D_SIZE : VPU_EXT_P_SIZE;
	u32 vpu_ext_mem0 = fw_type ? VPU_DMEM_EXT0_ADDR : VPU_PMEM_EXT0_ADDR;
	u32 vpu_ext_mem1 = fw_type ? VPU_DMEM_EXT1_ADDR : VPU_PMEM_EXT1_ADDR;
	u32 offset_4gb = vpu->enable_4GB ? 0x40000000 : 0;

	vpu->extmem[fw_type].va = dma_alloc_coherent(dev,
					       fw_ext_size,
					       &vpu->extmem[fw_type].pa,
					       GFP_KERNEL);
	if (!vpu->extmem[fw_type].va) {
		dev_err(dev, "Failed to allocate the extended program memory\n");
		return -ENOMEM;
	}

	/* Disable extend0. Enable extend1 */
	vpu_cfg_writel(vpu, 0x1, vpu_ext_mem0);
	vpu_cfg_writel(vpu, (vpu->extmem[fw_type].pa & 0xFFFFF000) + offset_4gb,
		       vpu_ext_mem1);

	dev_info(dev, "%s extend memory phy=0x%llx virt=0x%p\n",
		 fw_type ? "Data" : "Program",
		 (unsigned long long)vpu->extmem[fw_type].pa,
		 vpu->extmem[fw_type].va);

	return 0;
}

static void vpu_ipi_handler(struct mtk_vpu *vpu)
{
	struct share_obj __iomem *rcv_obj = vpu->recv_buf;
	struct vpu_ipi_desc *ipi_desc = vpu->ipi_desc;
	unsigned char data[SHARE_BUF_SIZE];
	s32 id = readl(&rcv_obj->id);

	memcpy_fromio(data, rcv_obj->share_buf, sizeof(data));
	if (id < IPI_MAX && ipi_desc[id].handler) {
		ipi_desc[id].handler(data, readl(&rcv_obj->len),
				     ipi_desc[id].priv);
		if (id > IPI_VPU_INIT) {
			vpu->ipi_id_ack[id] = true;
			wake_up(&vpu->ack_wq);
		}
	} else {
		dev_err(vpu->dev, "No such ipi id = %d\n", id);
	}
}

static int vpu_ipi_init(struct mtk_vpu *vpu)
{
	/* Disable VPU to host interrupt */
	vpu_cfg_writel(vpu, 0x0, VPU_TO_HOST);

	/* shared buffer initialization */
	vpu->recv_buf = vpu->reg.tcm + VPU_DTCM_OFFSET;
	vpu->send_buf = vpu->recv_buf + 1;
	memset_io(vpu->recv_buf, 0, sizeof(struct share_obj));
	memset_io(vpu->send_buf, 0, sizeof(struct share_obj));

	return 0;
}

static irqreturn_t vpu_irq_handler(int irq, void *priv)
{
	struct mtk_vpu *vpu = priv;
	u32 vpu_to_host;
	int ret;

	/*
	 * Clock should have been enabled already.
	 * Enable again in case vpu_ipi_send times out
	 * and has disabled the clock.
	 */
	ret = clk_enable(vpu->clk);
	if (ret) {
		dev_err(vpu->dev, "[VPU] enable clock failed %d\n", ret);
		return IRQ_NONE;
	}
	vpu_to_host = vpu_cfg_readl(vpu, VPU_TO_HOST);
	if (vpu_to_host & VPU_IPC_INT) {
		vpu_ipi_handler(vpu);
	} else {
		dev_err(vpu->dev, "vpu watchdog timeout! 0x%x", vpu_to_host);
		queue_work(vpu->wdt.wq, &vpu->wdt.ws);
	}

	/* VPU won't send another interrupt until we set VPU_TO_HOST to 0. */
	vpu_cfg_writel(vpu, 0x0, VPU_TO_HOST);
	clk_disable(vpu->clk);

	return IRQ_HANDLED;
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *vpu_debugfs;
#endif
static int mtk_vpu_probe(struct platform_device *pdev)
{
	struct mtk_vpu *vpu;
	struct device *dev;
	struct resource *res;
	int ret = 0;

	dev_dbg(&pdev->dev, "initialization\n");

	dev = &pdev->dev;
	vpu = devm_kzalloc(dev, sizeof(*vpu), GFP_KERNEL);
	if (!vpu)
		return -ENOMEM;

	vpu->dev = &pdev->dev;
	vpu->reg.tcm = devm_platform_ioremap_resource_byname(pdev, "tcm");
	if (IS_ERR((__force void *)vpu->reg.tcm))
		return PTR_ERR((__force void *)vpu->reg.tcm);

	vpu->reg.cfg = devm_platform_ioremap_resource_byname(pdev, "cfg_reg");
	if (IS_ERR((__force void *)vpu->reg.cfg))
		return PTR_ERR((__force void *)vpu->reg.cfg);

	/* Get VPU clock */
	vpu->clk = devm_clk_get(dev, "main");
	if (IS_ERR(vpu->clk)) {
		dev_err(dev, "get vpu clock failed\n");
		return PTR_ERR(vpu->clk);
	}

	platform_set_drvdata(pdev, vpu);

	ret = clk_prepare(vpu->clk);
	if (ret) {
		dev_err(dev, "prepare vpu clock failed\n");
		return ret;
	}

	/* VPU watchdog */
	vpu->wdt.wq = create_singlethread_workqueue("vpu_wdt");
	if (!vpu->wdt.wq) {
		dev_err(dev, "initialize wdt workqueue failed\n");
		return -ENOMEM;
	}
	INIT_WORK(&vpu->wdt.ws, vpu_wdt_reset_func);
	mutex_init(&vpu->vpu_mutex);

	ret = vpu_clock_enable(vpu);
	if (ret) {
		dev_err(dev, "enable vpu clock failed\n");
		goto workqueue_destroy;
	}

	dev_dbg(dev, "vpu ipi init\n");
	ret = vpu_ipi_init(vpu);
	if (ret) {
		dev_err(dev, "Failed to init ipi\n");
		goto disable_vpu_clk;
	}

	/* register vpu initialization IPI */
	ret = vpu_ipi_register(pdev, IPI_VPU_INIT, vpu_init_ipi_handler,
			       "vpu_init", vpu);
	if (ret) {
		dev_err(dev, "Failed to register IPI_VPU_INIT\n");
		goto vpu_mutex_destroy;
	}

#ifdef CONFIG_DEBUG_FS
	vpu_debugfs = debugfs_create_file("mtk_vpu", S_IRUGO, NULL, (void *)dev,
					  &vpu_debug_fops);
#endif

	/* Set PTCM to 96K and DTCM to 32K */
	vpu_cfg_writel(vpu, 0x2, VPU_TCM_CFG);

	vpu->enable_4GB = !!(totalram_pages() > (SZ_2G >> PAGE_SHIFT));
	dev_info(dev, "4GB mode %u\n", vpu->enable_4GB);

	if (vpu->enable_4GB) {
		ret = of_reserved_mem_device_init(dev);
		if (ret)
			dev_info(dev, "init reserved memory failed\n");
			/* continue to use dynamic allocation if failed */
	}

	ret = vpu_alloc_ext_mem(vpu, D_FW);
	if (ret) {
		dev_err(dev, "Allocate DM failed\n");
		goto remove_debugfs;
	}

	ret = vpu_alloc_ext_mem(vpu, P_FW);
	if (ret) {
		dev_err(dev, "Allocate PM failed\n");
		goto free_d_mem;
	}

	init_waitqueue_head(&vpu->run.wq);
	init_waitqueue_head(&vpu->ack_wq);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "get IRQ resource failed.\n");
		ret = -ENXIO;
		goto free_p_mem;
	}
	vpu->reg.irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, vpu->reg.irq, vpu_irq_handler, 0,
			       pdev->name, vpu);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto free_p_mem;
	}

	vpu_clock_disable(vpu);
	dev_dbg(dev, "initialization completed\n");

	return 0;

free_p_mem:
	vpu_free_ext_mem(vpu, P_FW);
free_d_mem:
	vpu_free_ext_mem(vpu, D_FW);
remove_debugfs:
	of_reserved_mem_device_release(dev);
#ifdef CONFIG_DEBUG_FS
	debugfs_remove(vpu_debugfs);
#endif
	memset(vpu->ipi_desc, 0, sizeof(struct vpu_ipi_desc) * IPI_MAX);
vpu_mutex_destroy:
	mutex_destroy(&vpu->vpu_mutex);
disable_vpu_clk:
	vpu_clock_disable(vpu);
workqueue_destroy:
	destroy_workqueue(vpu->wdt.wq);

	return ret;
}

static const struct of_device_id mtk_vpu_match[] = {
	{
		.compatible = "mediatek,mt8173-vpu",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_vpu_match);

static int mtk_vpu_remove(struct platform_device *pdev)
{
	struct mtk_vpu *vpu = platform_get_drvdata(pdev);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove(vpu_debugfs);
#endif
	if (vpu->wdt.wq) {
		flush_workqueue(vpu->wdt.wq);
		destroy_workqueue(vpu->wdt.wq);
	}
	vpu_free_ext_mem(vpu, P_FW);
	vpu_free_ext_mem(vpu, D_FW);
	mutex_destroy(&vpu->vpu_mutex);
	clk_unprepare(vpu->clk);

	return 0;
}

static int mtk_vpu_suspend(struct device *dev)
{
	struct mtk_vpu *vpu = dev_get_drvdata(dev);
	unsigned long timeout;
	int ret;

	ret = vpu_clock_enable(vpu);
	if (ret) {
		dev_err(dev, "failed to enable vpu clock\n");
		return ret;
	}

	if (!vpu_running(vpu)) {
		vpu_clock_disable(vpu);
		clk_unprepare(vpu->clk);
		return 0;
	}

	mutex_lock(&vpu->vpu_mutex);
	/* disable vpu timer interrupt */
	vpu_cfg_writel(vpu, vpu_cfg_readl(vpu, VPU_INT_STATUS) | VPU_IDLE_STATE,
		       VPU_INT_STATUS);
	/* check if vpu is idle for system suspend */
	timeout = jiffies + msecs_to_jiffies(VPU_IDLE_TIMEOUT_MS);
	do {
		if (time_after(jiffies, timeout)) {
			dev_err(dev, "vpu idle timeout\n");
			mutex_unlock(&vpu->vpu_mutex);
			vpu_clock_disable(vpu);
			return -EIO;
		}
	} while (!vpu_cfg_readl(vpu, VPU_IDLE_REG));

	mutex_unlock(&vpu->vpu_mutex);
	vpu_clock_disable(vpu);
	clk_unprepare(vpu->clk);

	return 0;
}

static int mtk_vpu_resume(struct device *dev)
{
	struct mtk_vpu *vpu = dev_get_drvdata(dev);
	int ret;

	clk_prepare(vpu->clk);
	ret = vpu_clock_enable(vpu);
	if (ret) {
		dev_err(dev, "failed to enable vpu clock\n");
		return ret;
	}

	mutex_lock(&vpu->vpu_mutex);
	/* enable vpu timer interrupt */
	vpu_cfg_writel(vpu,
		       vpu_cfg_readl(vpu, VPU_INT_STATUS) & ~(VPU_IDLE_STATE),
		       VPU_INT_STATUS);
	mutex_unlock(&vpu->vpu_mutex);
	vpu_clock_disable(vpu);

	return 0;
}

static const struct dev_pm_ops mtk_vpu_pm = {
	.suspend = mtk_vpu_suspend,
	.resume = mtk_vpu_resume,
};

static struct platform_driver mtk_vpu_driver = {
	.probe	= mtk_vpu_probe,
	.remove	= mtk_vpu_remove,
	.driver	= {
		.name	= "mtk_vpu",
		.pm = &mtk_vpu_pm,
		.of_match_table = mtk_vpu_match,
	},
};

module_platform_driver(mtk_vpu_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek Video Processor Unit driver");

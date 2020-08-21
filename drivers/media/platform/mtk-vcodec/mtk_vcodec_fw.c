// SPDX-License-Identifier: GPL-2.0

#include "mtk_vcodec_fw.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_drv.h"

struct mtk_vcodec_fw_ops {
	int (*load_firmware)(struct mtk_vcodec_fw *fw);
	unsigned int (*get_vdec_capa)(struct mtk_vcodec_fw *fw);
	unsigned int (*get_venc_capa)(struct mtk_vcodec_fw *fw);
	void * (*map_dm_addr)(struct mtk_vcodec_fw *fw, u32 dtcm_dmem_addr);
	int (*ipi_register)(struct mtk_vcodec_fw *fw, int id,
			    mtk_vcodec_ipi_handler handler, const char *name, void *priv);
	int (*ipi_send)(struct mtk_vcodec_fw *fw, int id, void *buf,
			unsigned int len, unsigned int wait);
};

struct mtk_vcodec_fw {
	enum mtk_vcodec_fw_type type;
	const struct mtk_vcodec_fw_ops *ops;
	struct platform_device *pdev;
	struct mtk_scp *scp;
};

static int mtk_vcodec_vpu_load_firmware(struct mtk_vcodec_fw *fw)
{
	return vpu_load_firmware(fw->pdev);
}

static unsigned int mtk_vcodec_vpu_get_vdec_capa(struct mtk_vcodec_fw *fw)
{
	return vpu_get_vdec_hw_capa(fw->pdev);
}

static unsigned int mtk_vcodec_vpu_get_venc_capa(struct mtk_vcodec_fw *fw)
{
	return vpu_get_venc_hw_capa(fw->pdev);
}

static void *mtk_vcodec_vpu_map_dm_addr(struct mtk_vcodec_fw *fw,
					u32 dtcm_dmem_addr)
{
	return vpu_mapping_dm_addr(fw->pdev, dtcm_dmem_addr);
}

static int mtk_vcodec_vpu_set_ipi_register(struct mtk_vcodec_fw *fw, int id,
					   mtk_vcodec_ipi_handler handler,
					   const char *name, void *priv)
{
	/*
	 * The handler we receive takes a void * as its first argument. We
	 * cannot change this because it needs to be passed down to the rproc
	 * subsystem when SCP is used. VPU takes a const argument, which is
	 * more constrained, so the conversion below is safe.
	 */
	ipi_handler_t handler_const = (ipi_handler_t)handler;

	return vpu_ipi_register(fw->pdev, id, handler_const, name, priv);
}

static int mtk_vcodec_vpu_ipi_send(struct mtk_vcodec_fw *fw, int id, void *buf,
				   unsigned int len, unsigned int wait)
{
	return vpu_ipi_send(fw->pdev, id, buf, len);
}

static const struct mtk_vcodec_fw_ops mtk_vcodec_vpu_msg = {
	.load_firmware = mtk_vcodec_vpu_load_firmware,
	.get_vdec_capa = mtk_vcodec_vpu_get_vdec_capa,
	.get_venc_capa = mtk_vcodec_vpu_get_venc_capa,
	.map_dm_addr = mtk_vcodec_vpu_map_dm_addr,
	.ipi_register = mtk_vcodec_vpu_set_ipi_register,
	.ipi_send = mtk_vcodec_vpu_ipi_send,
};

static int mtk_vcodec_scp_load_firmware(struct mtk_vcodec_fw *fw)
{
	return rproc_boot(scp_get_rproc(fw->scp));
}

static unsigned int mtk_vcodec_scp_get_vdec_capa(struct mtk_vcodec_fw *fw)
{
	return scp_get_vdec_hw_capa(fw->scp);
}

static unsigned int mtk_vcodec_scp_get_venc_capa(struct mtk_vcodec_fw *fw)
{
	return scp_get_venc_hw_capa(fw->scp);
}

static void *mtk_vcodec_vpu_scp_dm_addr(struct mtk_vcodec_fw *fw,
					u32 dtcm_dmem_addr)
{
	return scp_mapping_dm_addr(fw->scp, dtcm_dmem_addr);
}

static int mtk_vcodec_scp_set_ipi_register(struct mtk_vcodec_fw *fw, int id,
					   mtk_vcodec_ipi_handler handler,
					   const char *name, void *priv)
{
	return scp_ipi_register(fw->scp, id, handler, priv);
}

static int mtk_vcodec_scp_ipi_send(struct mtk_vcodec_fw *fw, int id, void *buf,
				   unsigned int len, unsigned int wait)
{
	return scp_ipi_send(fw->scp, id, buf, len, wait);
}

static const struct mtk_vcodec_fw_ops mtk_vcodec_rproc_msg = {
	.load_firmware = mtk_vcodec_scp_load_firmware,
	.get_vdec_capa = mtk_vcodec_scp_get_vdec_capa,
	.get_venc_capa = mtk_vcodec_scp_get_venc_capa,
	.map_dm_addr = mtk_vcodec_vpu_scp_dm_addr,
	.ipi_register = mtk_vcodec_scp_set_ipi_register,
	.ipi_send = mtk_vcodec_scp_ipi_send,
};

static void mtk_vcodec_reset_handler(void *priv)
{
	struct mtk_vcodec_dev *dev = priv;
	struct mtk_vcodec_ctx *ctx;

	mtk_v4l2_err("Watchdog timeout!!");

	mutex_lock(&dev->dev_mutex);
	list_for_each_entry(ctx, &dev->ctx_list, list) {
		ctx->state = MTK_STATE_ABORT;
		mtk_v4l2_debug(0, "[%d] Change to state MTK_STATE_ABORT",
			       ctx->id);
	}
	mutex_unlock(&dev->dev_mutex);
}

struct mtk_vcodec_fw *mtk_vcodec_fw_select(struct mtk_vcodec_dev *dev,
					   enum mtk_vcodec_fw_type type,
					   enum rst_id rst_id)
{
	const struct mtk_vcodec_fw_ops *ops;
	struct mtk_vcodec_fw *fw;
	struct platform_device *fw_pdev = NULL;
	struct mtk_scp *scp = NULL;

	switch (type) {
	case VPU:
		ops = &mtk_vcodec_vpu_msg;
		fw_pdev = vpu_get_plat_device(dev->plat_dev);
		if (!fw_pdev) {
			mtk_v4l2_err("firmware device is not ready");
			return ERR_PTR(-EINVAL);
		}
		vpu_wdt_reg_handler(fw_pdev, mtk_vcodec_reset_handler,
				    dev, rst_id);
		break;
	case SCP:
		ops = &mtk_vcodec_rproc_msg;
		scp = scp_get(dev->plat_dev);
		if (!scp) {
			mtk_v4l2_err("could not get vdec scp handle");
			return ERR_PTR(-EPROBE_DEFER);
		}
		break;
	default:
		mtk_v4l2_err("invalid vcodec fw type");
		return ERR_PTR(-EINVAL);
	}

	fw = devm_kzalloc(&dev->plat_dev->dev, sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return ERR_PTR(-EINVAL);

	fw->type = type;
	fw->ops = ops;
	fw->pdev = fw_pdev;
	fw->scp = scp;

	return fw;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_select);

void mtk_vcodec_fw_release(struct mtk_vcodec_fw *fw)
{
	switch (fw->type) {
	case VPU:
		put_device(&fw->pdev->dev);
		break;
	case SCP:
		scp_put(fw->scp);
		break;
	}
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_release);

int mtk_vcodec_fw_load_firmware(struct mtk_vcodec_fw *fw)
{
	return fw->ops->load_firmware(fw);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_load_firmware);

unsigned int mtk_vcodec_fw_get_vdec_capa(struct mtk_vcodec_fw *fw)
{
	return fw->ops->get_vdec_capa(fw);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_get_vdec_capa);

unsigned int mtk_vcodec_fw_get_venc_capa(struct mtk_vcodec_fw *fw)
{
	return fw->ops->get_venc_capa(fw);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_get_venc_capa);

void *mtk_vcodec_fw_map_dm_addr(struct mtk_vcodec_fw *fw, u32 mem_addr)
{
	return fw->ops->map_dm_addr(fw, mem_addr);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_map_dm_addr);

int mtk_vcodec_fw_ipi_register(struct mtk_vcodec_fw *fw, int id,
			       mtk_vcodec_ipi_handler handler,
			       const char *name, void *priv)
{
	return fw->ops->ipi_register(fw, id, handler, name, priv);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_ipi_register);

int mtk_vcodec_fw_ipi_send(struct mtk_vcodec_fw *fw, int id, void *buf,
			   unsigned int len, unsigned int wait)
{
	return fw->ops->ipi_send(fw, id, buf, len, wait);
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_ipi_send);

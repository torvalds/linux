// SPDX-License-Identifier: GPL-2.0

#include "mtk_vcodec_fw_priv.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_drv.h"

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

static void mtk_vcodec_vpu_release(struct mtk_vcodec_fw *fw)
{
	put_device(&fw->pdev->dev);
}

static void mtk_vcodec_vpu_reset_handler(void *priv)
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

static const struct mtk_vcodec_fw_ops mtk_vcodec_vpu_msg = {
	.load_firmware = mtk_vcodec_vpu_load_firmware,
	.get_vdec_capa = mtk_vcodec_vpu_get_vdec_capa,
	.get_venc_capa = mtk_vcodec_vpu_get_venc_capa,
	.map_dm_addr = mtk_vcodec_vpu_map_dm_addr,
	.ipi_register = mtk_vcodec_vpu_set_ipi_register,
	.ipi_send = mtk_vcodec_vpu_ipi_send,
	.release = mtk_vcodec_vpu_release,
};

struct mtk_vcodec_fw *mtk_vcodec_fw_vpu_init(struct mtk_vcodec_dev *dev,
					     enum mtk_vcodec_fw_use fw_use)
{
	struct platform_device *fw_pdev;
	struct mtk_vcodec_fw *fw;
	enum rst_id rst_id;

	switch (fw_use) {
	case ENCODER:
		rst_id = VPU_RST_ENC;
		break;
	case DECODER:
	default:
		rst_id = VPU_RST_DEC;
		break;
	}

	fw_pdev = vpu_get_plat_device(dev->plat_dev);
	if (!fw_pdev) {
		mtk_v4l2_err("firmware device is not ready");
		return ERR_PTR(-EINVAL);
	}
	vpu_wdt_reg_handler(fw_pdev, mtk_vcodec_vpu_reset_handler, dev, rst_id);

	fw = devm_kzalloc(&dev->plat_dev->dev, sizeof(*fw), GFP_KERNEL);
	fw->type = VPU;
	fw->ops = &mtk_vcodec_vpu_msg;
	fw->pdev = fw_pdev;

	return fw;
}

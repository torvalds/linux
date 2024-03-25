// SPDX-License-Identifier: GPL-2.0

#include "../decoder/mtk_vcodec_dec_drv.h"
#include "../encoder/mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_fw_priv.h"

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
	return vpu_ipi_register(fw->pdev, id, handler, name, priv);
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

static void mtk_vcodec_vpu_reset_dec_handler(void *priv)
{
	struct mtk_vcodec_dec_dev *dev = priv;
	struct mtk_vcodec_dec_ctx *ctx;

	dev_err(&dev->plat_dev->dev, "Watchdog timeout!!");

	mutex_lock(&dev->dev_mutex);
	list_for_each_entry(ctx, &dev->ctx_list, list) {
		ctx->state = MTK_STATE_ABORT;
		mtk_v4l2_vdec_dbg(0, ctx, "[%d] Change to state MTK_STATE_ABORT", ctx->id);
	}
	mutex_unlock(&dev->dev_mutex);
}

static void mtk_vcodec_vpu_reset_enc_handler(void *priv)
{
	struct mtk_vcodec_enc_dev *dev = priv;
	struct mtk_vcodec_enc_ctx *ctx;

	dev_err(&dev->plat_dev->dev, "Watchdog timeout!!");

	mutex_lock(&dev->dev_mutex);
	list_for_each_entry(ctx, &dev->ctx_list, list) {
		ctx->state = MTK_STATE_ABORT;
		mtk_v4l2_vdec_dbg(0, ctx, "[%d] Change to state MTK_STATE_ABORT", ctx->id);
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

struct mtk_vcodec_fw *mtk_vcodec_fw_vpu_init(void *priv, enum mtk_vcodec_fw_use fw_use)
{
	struct platform_device *fw_pdev;
	struct platform_device *plat_dev;
	struct mtk_vcodec_fw *fw;
	enum rst_id rst_id;

	if (fw_use == ENCODER) {
		struct mtk_vcodec_enc_dev *enc_dev = priv;

		plat_dev = enc_dev->plat_dev;
		rst_id = VPU_RST_ENC;
	} else if (fw_use == DECODER) {
		struct mtk_vcodec_dec_dev *dec_dev = priv;

		plat_dev = dec_dev->plat_dev;
		rst_id = VPU_RST_DEC;
	} else {
		pr_err("Invalid fw_use %d (use a reasonable fw id here)\n", fw_use);
		return ERR_PTR(-EINVAL);
	}

	fw_pdev = vpu_get_plat_device(plat_dev);
	if (!fw_pdev) {
		dev_err(&plat_dev->dev, "firmware device is not ready");
		return ERR_PTR(-EINVAL);
	}

	if (fw_use == DECODER)
		vpu_wdt_reg_handler(fw_pdev, mtk_vcodec_vpu_reset_dec_handler, priv, rst_id);
	else
		vpu_wdt_reg_handler(fw_pdev, mtk_vcodec_vpu_reset_enc_handler, priv, rst_id);

	fw = devm_kzalloc(&plat_dev->dev, sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return ERR_PTR(-ENOMEM);
	fw->type = VPU;
	fw->ops = &mtk_vcodec_vpu_msg;
	fw->pdev = fw_pdev;
	fw->fw_use = fw_use;

	return fw;
}

// SPDX-License-Identifier: GPL-2.0

#include "../decoder/mtk_vcodec_dec_drv.h"
#include "../encoder/mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_fw_priv.h"

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

static void mtk_vcodec_scp_release(struct mtk_vcodec_fw *fw)
{
	scp_put(fw->scp);
}

static const struct mtk_vcodec_fw_ops mtk_vcodec_rproc_msg = {
	.load_firmware = mtk_vcodec_scp_load_firmware,
	.get_vdec_capa = mtk_vcodec_scp_get_vdec_capa,
	.get_venc_capa = mtk_vcodec_scp_get_venc_capa,
	.map_dm_addr = mtk_vcodec_vpu_scp_dm_addr,
	.ipi_register = mtk_vcodec_scp_set_ipi_register,
	.ipi_send = mtk_vcodec_scp_ipi_send,
	.release = mtk_vcodec_scp_release,
};

struct mtk_vcodec_fw *mtk_vcodec_fw_scp_init(void *priv, enum mtk_vcodec_fw_use fw_use)
{
	struct mtk_vcodec_fw *fw;
	struct platform_device *plat_dev;
	struct mtk_scp *scp;

	if (fw_use == ENCODER) {
		struct mtk_vcodec_enc_dev *enc_dev = priv;

		plat_dev = enc_dev->plat_dev;
	} else if (fw_use == DECODER) {
		struct mtk_vcodec_dec_dev *dec_dev = priv;

		plat_dev = dec_dev->plat_dev;
	} else {
		pr_err("Invalid fw_use %d (use a reasonable fw id here)\n", fw_use);
		return ERR_PTR(-EINVAL);
	}

	scp = scp_get(plat_dev);
	if (!scp) {
		dev_err(&plat_dev->dev, "could not get vdec scp handle");
		return ERR_PTR(-EPROBE_DEFER);
	}

	fw = devm_kzalloc(&plat_dev->dev, sizeof(*fw), GFP_KERNEL);
	if (!fw) {
		scp_put(scp);
		return ERR_PTR(-ENOMEM);
	}

	fw->type = SCP;
	fw->ops = &mtk_vcodec_rproc_msg;
	fw->scp = scp;

	return fw;
}

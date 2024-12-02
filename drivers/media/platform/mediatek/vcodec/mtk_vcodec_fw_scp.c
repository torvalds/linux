// SPDX-License-Identifier: GPL-2.0

#include "mtk_vcodec_fw_priv.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_drv.h"

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

struct mtk_vcodec_fw *mtk_vcodec_fw_scp_init(struct mtk_vcodec_dev *dev)
{
	struct mtk_vcodec_fw *fw;
	struct mtk_scp *scp;

	scp = scp_get(dev->plat_dev);
	if (!scp) {
		mtk_v4l2_err("could not get vdec scp handle");
		return ERR_PTR(-EPROBE_DEFER);
	}

	fw = devm_kzalloc(&dev->plat_dev->dev, sizeof(*fw), GFP_KERNEL);
	fw->type = SCP;
	fw->ops = &mtk_vcodec_rproc_msg;
	fw->scp = scp;

	return fw;
}

/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MTK_VCODEC_FW_PRIV_H_
#define _MTK_VCODEC_FW_PRIV_H_

#include "mtk_vcodec_fw.h"

struct mtk_vcodec_dev;

struct mtk_vcodec_fw {
	enum mtk_vcodec_fw_type type;
	const struct mtk_vcodec_fw_ops *ops;
	struct platform_device *pdev;
	struct mtk_scp *scp;
};

struct mtk_vcodec_fw_ops {
	int (*load_firmware)(struct mtk_vcodec_fw *fw);
	unsigned int (*get_vdec_capa)(struct mtk_vcodec_fw *fw);
	unsigned int (*get_venc_capa)(struct mtk_vcodec_fw *fw);
	void *(*map_dm_addr)(struct mtk_vcodec_fw *fw, u32 dtcm_dmem_addr);
	int (*ipi_register)(struct mtk_vcodec_fw *fw, int id,
			    mtk_vcodec_ipi_handler handler, const char *name,
			    void *priv);
	int (*ipi_send)(struct mtk_vcodec_fw *fw, int id, void *buf,
			unsigned int len, unsigned int wait);
	void (*release)(struct mtk_vcodec_fw *fw);
};

#if IS_ENABLED(CONFIG_VIDEO_MEDIATEK_VCODEC_VPU)
struct mtk_vcodec_fw *mtk_vcodec_fw_vpu_init(struct mtk_vcodec_dev *dev,
					     enum mtk_vcodec_fw_use fw_use);
#else
static inline struct mtk_vcodec_fw *
mtk_vcodec_fw_vpu_init(struct mtk_vcodec_dev *dev,
		       enum mtk_vcodec_fw_use fw_use)
{
	return ERR_PTR(-ENODEV);
}
#endif /* CONFIG_VIDEO_MEDIATEK_VCODEC_VPU */

#if IS_ENABLED(CONFIG_VIDEO_MEDIATEK_VCODEC_SCP)
struct mtk_vcodec_fw *mtk_vcodec_fw_scp_init(struct mtk_vcodec_dev *dev);
#else
static inline struct mtk_vcodec_fw *
mtk_vcodec_fw_scp_init(struct mtk_vcodec_dev *dev)
{
	return ERR_PTR(-ENODEV);
}
#endif /* CONFIG_VIDEO_MEDIATEK_VCODEC_SCP */

#endif /* _MTK_VCODEC_FW_PRIV_H_ */

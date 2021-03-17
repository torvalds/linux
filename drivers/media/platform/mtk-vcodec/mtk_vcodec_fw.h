/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MTK_VCODEC_FW_H_
#define _MTK_VCODEC_FW_H_

#include <linux/remoteproc.h>
#include <linux/remoteproc/mtk_scp.h>

#include "../mtk-vpu/mtk_vpu.h"

struct mtk_vcodec_dev;

enum mtk_vcodec_fw_type {
	VPU,
	SCP,
};

enum mtk_vcodec_fw_use {
	DECODER,
	ENCODER,
};

struct mtk_vcodec_fw;

typedef void (*mtk_vcodec_ipi_handler) (void *data,
	unsigned int len, void *priv);

struct mtk_vcodec_fw *mtk_vcodec_fw_select(struct mtk_vcodec_dev *dev,
					   enum mtk_vcodec_fw_type type,
					   enum mtk_vcodec_fw_use fw_use);
void mtk_vcodec_fw_release(struct mtk_vcodec_fw *fw);

int mtk_vcodec_fw_load_firmware(struct mtk_vcodec_fw *fw);
unsigned int mtk_vcodec_fw_get_vdec_capa(struct mtk_vcodec_fw *fw);
unsigned int mtk_vcodec_fw_get_venc_capa(struct mtk_vcodec_fw *fw);
void *mtk_vcodec_fw_map_dm_addr(struct mtk_vcodec_fw *fw, u32 mem_addr);
int mtk_vcodec_fw_ipi_register(struct mtk_vcodec_fw *fw, int id,
			       mtk_vcodec_ipi_handler handler,
			       const char *name, void *priv);
int mtk_vcodec_fw_ipi_send(struct mtk_vcodec_fw *fw, int id,
			   void *buf, unsigned int len, unsigned int wait);

#endif /* _MTK_VCODEC_FW_H_ */

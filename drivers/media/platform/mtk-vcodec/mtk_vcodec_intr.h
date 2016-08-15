/*
* Copyright (c) 2016 MediaTek Inc.
* Author: Tiffany Lin <tiffany.lin@mediatek.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#ifndef _MTK_VCODEC_INTR_H_
#define _MTK_VCODEC_INTR_H_

#define MTK_INST_IRQ_RECEIVED		0x1
#define MTK_INST_WORK_THREAD_ABORT_DONE	0x2

struct mtk_vcodec_ctx;

/* timeout is ms */
int mtk_vcodec_wait_for_done_ctx(struct mtk_vcodec_ctx *data, int command,
				unsigned int timeout_ms);

#endif /* _MTK_VCODEC_INTR_H_ */

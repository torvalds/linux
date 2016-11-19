/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef HVA_HW_H
#define HVA_HW_H

#include "hva-mem.h"

/* HVA Versions */
#define HVA_VERSION_UNKNOWN    0x000
#define HVA_VERSION_V400       0x400

/* HVA command types */
enum hva_hw_cmd_type {
	/* RESERVED = 0x00 */
	/* RESERVED = 0x01 */
	H264_ENC = 0x02,
	/* RESERVED = 0x03 */
	/* RESERVED = 0x04 */
	/* RESERVED = 0x05 */
	/* RESERVED = 0x06 */
	/* RESERVED = 0x07 */
	REMOVE_CLIENT = 0x08,
	FREEZE_CLIENT = 0x09,
	START_CLIENT = 0x0A,
	FREEZE_ALL = 0x0B,
	START_ALL = 0x0C,
	REMOVE_ALL = 0x0D
};

int hva_hw_probe(struct platform_device *pdev, struct hva_dev *hva);
void hva_hw_remove(struct hva_dev *hva);
int hva_hw_runtime_suspend(struct device *dev);
int hva_hw_runtime_resume(struct device *dev);
int hva_hw_execute_task(struct hva_ctx *ctx, enum hva_hw_cmd_type cmd,
			struct hva_buffer *task);

#endif /* HVA_HW_H */

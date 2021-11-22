/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_ESPARSER_H_
#define __MESON_VDEC_ESPARSER_H_

#include <linux/platform_device.h>

#include "vdec.h"

int esparser_init(struct platform_device *pdev, struct amvdec_core *core);
int esparser_power_up(struct amvdec_session *sess);

/**
 * esparser_queue_eos() - write End Of Stream sequence to the ESPARSER
 *
 * @core: vdec core struct
 * @data: EOS sequence
 * @len: length of EOS sequence
 */
int esparser_queue_eos(struct amvdec_core *core, const u8 *data, u32 len);

/**
 * esparser_queue_all_src() - work handler that writes as many src buffers
 * as possible to the ESPARSER
 *
 * @work: work struct
 */
void esparser_queue_all_src(struct work_struct *work);

#define ESPARSER_MIN_PACKET_SIZE SZ_4K

#endif

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _LOGICVC_MODE_H_
#define _LOGICVC_MODE_H_

struct logicvc_drm;

int logicvc_mode_init(struct logicvc_drm *logicvc);
void logicvc_mode_fini(struct logicvc_drm *logicvc);

#endif

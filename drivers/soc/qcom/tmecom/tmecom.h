/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _TMECOM_H_
#define _TMECOM_H_

#define MBOX_MAX_MSG_LEN	1024

int tmecom_process_request(const void *reqbuf, size_t reqsize, void *respbuf,
		size_t *respsize);
#endif  /*_TMECOM_H_ */

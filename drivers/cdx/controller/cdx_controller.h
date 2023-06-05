/* SPDX-License-Identifier: GPL-2.0
 *
 * Header file for the CDX Controller
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef _CDX_CONTROLLER_H_
#define _CDX_CONTROLLER_H_

#include <linux/cdx/cdx_bus.h>
#include "mcdi_functions.h"

void cdx_rpmsg_post_probe(struct cdx_controller *cdx);

void cdx_rpmsg_pre_remove(struct cdx_controller *cdx);

int cdx_rpmsg_send(struct cdx_mcdi *cdx_mcdi,
		   const struct cdx_dword *hdr, size_t hdr_len,
		   const struct cdx_dword *sdu, size_t sdu_len);

void cdx_rpmsg_read_resp(struct cdx_mcdi *cdx_mcdi,
			 struct cdx_dword *outbuf, size_t offset,
			 size_t outlen);

int cdx_setup_rpmsg(struct platform_device *pdev);

void cdx_destroy_rpmsg(struct platform_device *pdev);

#endif /* _CDX_CONT_PRIV_H_ */

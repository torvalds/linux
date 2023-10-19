/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-ctrls.h - control support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_CTRLS_H_
#define _VIVID_CTRLS_H_

enum vivid_hw_seek_modes {
	VIVID_HW_SEEK_BOUNDED,
	VIVID_HW_SEEK_WRAP,
	VIVID_HW_SEEK_BOTH,
};

int vivid_create_controls(struct vivid_dev *dev, bool show_ccs_cap,
		bool show_ccs_out, bool no_error_inj,
		bool has_sdtv, bool has_hdmi);
void vivid_free_controls(struct vivid_dev *dev);

#endif

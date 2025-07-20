/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_HDCP_GSC_MESSAGE_H__
#define __INTEL_HDCP_GSC_MESSAGE_H__

struct intel_display;

int intel_hdcp_gsc_init(struct intel_display *display);
void intel_hdcp_gsc_fini(struct intel_display *display);

#endif /* __INTEL_HDCP_GSC_MESSAGE_H__ */

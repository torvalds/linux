/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-cec.h - A Virtual Video Test Driver, cec emulation
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifdef CONFIG_VIDEO_VIVID_CEC
struct cec_adapter *vivid_cec_alloc_adap(struct vivid_dev *dev,
					 unsigned int idx,
					 bool is_source);
void vivid_cec_bus_free_work(struct vivid_dev *dev);

#else

static inline void vivid_cec_bus_free_work(struct vivid_dev *dev)
{
}

#endif

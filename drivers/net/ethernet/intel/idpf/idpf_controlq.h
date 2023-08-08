/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_CONTROLQ_H_
#define _IDPF_CONTROLQ_H_

struct idpf_hw {
	void __iomem *hw_addr;
	resource_size_t hw_addr_len;

	struct idpf_adapter *back;
};

#endif /* _IDPF_CONTROLQ_H_ */

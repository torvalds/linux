/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */
#ifndef _ENA_COMMON_H_
#define _ENA_COMMON_H_

#define ENA_COMMON_SPEC_VERSION_MAJOR        2
#define ENA_COMMON_SPEC_VERSION_MINOR        0

/* ENA operates with 48-bit memory addresses. ena_mem_addr_t */
struct ena_common_mem_addr {
	u32 mem_addr_low;

	u16 mem_addr_high;

	/* MBZ */
	u16 reserved16;
};

#endif /* _ENA_COMMON_H_ */

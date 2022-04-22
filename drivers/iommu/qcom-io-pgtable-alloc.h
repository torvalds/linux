/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_IO_PGTABLE_ALLOC_H
#define __QCOM_IO_PGTABLE_ALLOC_H

int qcom_io_pgtable_allocator_register(u32 vmid);
void qcom_io_pgtable_allocator_unregister(u32 vmid);
struct page *qcom_io_pgtable_alloc_page(u32 vmid, gfp_t gfp);
void qcom_io_pgtable_free_page(struct page *page);
int qcom_io_pgtable_alloc_init(void);
void qcom_io_pgtable_alloc_exit(void);

#endif /* __QCOM_IO_PGTABLE_ALLOC_H */


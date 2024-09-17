/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google LLC
 * Author: Keir Fraser <keirf@google.com>
 */

#ifndef __ASM_MEM_RELINQUISH_H
#define __ASM_MEM_RELINQUISH_H

struct page;

bool kvm_has_memrelinquish_services(void);
void page_relinquish(struct page *page);
void post_page_relinquish_tlb_inv(void);

#endif	/* __ASM_MEM_RELINQUISH_H */

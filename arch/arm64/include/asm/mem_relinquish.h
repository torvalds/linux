/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google LLC
 * Author: Keir Fraser <keirf@google.com>
 */

#ifndef __ASM_MEM_RELINQUISH_H
#define __ASM_MEM_RELINQUISH_H

struct page;

void page_relinquish(struct page *page);

#endif	/* __ASM_MEM_RELINQUISH_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_MMU_PRIVATE_H
#define __IA_CSS_MMU_PRIVATE_H

#include "system_local.h"

/*
 * This function sets the L1 pagetable address.
 * After power-up of the ISP the L1 pagetable can be set.
 * Once being set the L1 pagetable is protected against
 * further modifications.
 */
void
sh_css_mmu_set_page_table_base_index(hrt_data base_index);

#endif /* __IA_CSS_MMU_PRIVATE_H */

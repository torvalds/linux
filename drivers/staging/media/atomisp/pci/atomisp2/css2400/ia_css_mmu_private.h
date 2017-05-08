#ifdef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
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
#endif

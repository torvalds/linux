/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_LLH_H
#define HW_ATL2_LLH_H

#include <linux/types.h>

struct aq_hw_s;

/* get data from firmware shared input buffer */
void hw_atl2_mif_shared_buf_get(struct aq_hw_s *aq_hw, int offset, u32 *data,
				int len);

/* set data into firmware shared input buffer */
void hw_atl2_mif_shared_buf_write(struct aq_hw_s *aq_hw, int offset, u32 *data,
				  int len);

/* get data from firmware shared output buffer */
void hw_atl2_mif_shared_buf_read(struct aq_hw_s *aq_hw, int offset, u32 *data,
				 int len);

/* set host finished write shared buffer indication */
void hw_atl2_mif_host_finished_write_set(struct aq_hw_s *aq_hw, u32 finish);

/* get mcp finished read shared buffer indication */
u32 hw_atl2_mif_mcp_finished_read_get(struct aq_hw_s *aq_hw);

#endif /* HW_ATL2_LLH_H */

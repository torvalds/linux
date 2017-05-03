/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HW_QUEUE_DEFS_PLAT_H__
#define __HW_QUEUE_DEFS_PLAT_H__


/*****************************/
/* Descriptor packing macros */
/*****************************/

#define HW_QUEUE_FREE_SLOTS_GET() (CC_HAL_READ_REGISTER(CC_REG_OFFSET(CRY_KERNEL, DSCRPTR_QUEUE_CONTENT)) & HW_QUEUE_SLOTS_MAX)

#define HW_QUEUE_POLL_QUEUE_UNTIL_FREE_SLOTS(seqLen)						\
	do {											\
	} while (HW_QUEUE_FREE_SLOTS_GET() < (seqLen))

#define HW_DESC_PUSH_TO_QUEUE(pDesc) do {        				  \
	LOG_HW_DESC(pDesc);							  \
	HW_DESC_DUMP(pDesc);							  \
	CC_HAL_WRITE_REGISTER(GET_HW_Q_DESC_WORD_IDX(0), (pDesc)->word[0]); \
	CC_HAL_WRITE_REGISTER(GET_HW_Q_DESC_WORD_IDX(1), (pDesc)->word[1]); \
	CC_HAL_WRITE_REGISTER(GET_HW_Q_DESC_WORD_IDX(2), (pDesc)->word[2]); \
	CC_HAL_WRITE_REGISTER(GET_HW_Q_DESC_WORD_IDX(3), (pDesc)->word[3]); \
	CC_HAL_WRITE_REGISTER(GET_HW_Q_DESC_WORD_IDX(4), (pDesc)->word[4]); \
	wmb();									   \
	CC_HAL_WRITE_REGISTER(GET_HW_Q_DESC_WORD_IDX(5), (pDesc)->word[5]); \
} while (0)

#endif /*__HW_QUEUE_DEFS_PLAT_H__*/

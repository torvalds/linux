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

#ifndef __SSI_SRAM_MGR_H__
#define __SSI_SRAM_MGR_H__


#ifndef SSI_CC_SRAM_SIZE
#define SSI_CC_SRAM_SIZE 4096
#endif

struct ssi_drvdata;

/**
 * Address (offset) within CC internal SRAM
 */

typedef uint64_t ssi_sram_addr_t;

#define NULL_SRAM_ADDR ((ssi_sram_addr_t)-1)

/*!
 * Initializes SRAM pool.
 * The first X bytes of SRAM are reserved for ROM usage, hence, pool
 * starts right after X bytes.
 *
 * \param drvdata
 *
 * \return int Zero for success, negative value otherwise.
 */
int ssi_sram_mgr_init(struct ssi_drvdata *drvdata);

/*!
 * Uninits SRAM pool.
 *
 * \param drvdata
 */
void ssi_sram_mgr_fini(struct ssi_drvdata *drvdata);

/*!
 * Allocated buffer from SRAM pool.
 * Note: Caller is responsible to free the LAST allocated buffer.
 * This function does not taking care of any fragmentation may occur
 * by the order of calls to alloc/free.
 *
 * \param drvdata
 * \param size The requested bytes to allocate
 */
ssi_sram_addr_t ssi_sram_mgr_alloc(struct ssi_drvdata *drvdata, uint32_t size);

/**
 * ssi_sram_mgr_const2sram_desc() - Create const descriptors sequence to
 *	set values in given array into SRAM.
 * Note: each const value can't exceed word size.
 *
 * @src:	  A pointer to array of words to set as consts.
 * @dst:	  The target SRAM buffer to set into
 * @nelements:	  The number of words in "src" array
 * @seq:	  A pointer to the given IN/OUT descriptor sequence
 * @seq_len:	  A pointer to the given IN/OUT sequence length
 */
void ssi_sram_mgr_const2sram_desc(
	const uint32_t *src, ssi_sram_addr_t dst,
	unsigned int nelement,
	HwDesc_s *seq, unsigned int *seq_len);

#endif /*__SSI_SRAM_MGR_H__*/

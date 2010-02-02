/*****************************************************************************
* Copyright 2003 - 2009 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/
#ifndef NAND_BCM_UMI_H
#define NAND_BCM_UMI_H

/* ---- Include Files ---------------------------------------------------- */
#include <mach/reg_umi.h>
#include <mach/reg_nand.h>
#include <cfg_global.h>

/* ---- Constants and Types ---------------------------------------------- */
#if (CFG_GLOBAL_CHIP_FAMILY == CFG_GLOBAL_CHIP_FAMILY_BCMRING)
#define NAND_ECC_BCH (CFG_GLOBAL_CHIP_REV > 0xA0)
#else
#define NAND_ECC_BCH 0
#endif

#define CFG_GLOBAL_NAND_ECC_BCH_NUM_BYTES	13

#if NAND_ECC_BCH
#ifdef BOOT0_BUILD
#define NAND_ECC_NUM_BYTES 13
#else
#define NAND_ECC_NUM_BYTES CFG_GLOBAL_NAND_ECC_BCH_NUM_BYTES
#endif
#else
#define NAND_ECC_NUM_BYTES 3
#endif

#define NAND_DATA_ACCESS_SIZE 512

/* ---- Variable Externs ------------------------------------------ */
/* ---- Function Prototypes --------------------------------------- */
int nand_bcm_umi_bch_correct_page(uint8_t *datap, uint8_t *readEccData,
				  int numEccBytes);

/* Check in device is ready */
static inline int nand_bcm_umi_dev_ready(void)
{
	return REG_UMI_NAND_RCSR & REG_UMI_NAND_RCSR_RDY;
}

/* Wait until device is ready */
static inline void nand_bcm_umi_wait_till_ready(void)
{
	while (nand_bcm_umi_dev_ready() == 0)
		;
}

/* Enable Hamming ECC */
static inline void nand_bcm_umi_hamming_enable_hwecc(void)
{
	/* disable and reset ECC, 512 byte page */
	REG_UMI_NAND_ECC_CSR &= ~(REG_UMI_NAND_ECC_CSR_ECC_ENABLE |
		REG_UMI_NAND_ECC_CSR_256BYTE);
	/* enable ECC */
	REG_UMI_NAND_ECC_CSR |= REG_UMI_NAND_ECC_CSR_ECC_ENABLE;
}

#if NAND_ECC_BCH
/* BCH ECC specifics */
#define ECC_BITS_PER_CORRECTABLE_BIT 13

/* Enable BCH Read ECC */
static inline void nand_bcm_umi_bch_enable_read_hwecc(void)
{
	/* disable and reset ECC */
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_RD_ECC_VALID;
	/* Turn on ECC */
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_ECC_RD_EN;
}

/* Enable BCH Write ECC */
static inline void nand_bcm_umi_bch_enable_write_hwecc(void)
{
	/* disable and reset ECC */
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_WR_ECC_VALID;
	/* Turn on ECC */
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_ECC_WR_EN;
}

/* Config number of BCH ECC bytes */
static inline void nand_bcm_umi_bch_config_ecc(uint8_t numEccBytes)
{
	uint32_t nValue;
	uint32_t tValue;
	uint32_t kValue;
	uint32_t numBits = numEccBytes * 8;

	/* disable and reset ECC */
	REG_UMI_BCH_CTRL_STATUS =
	    REG_UMI_BCH_CTRL_STATUS_WR_ECC_VALID |
	    REG_UMI_BCH_CTRL_STATUS_RD_ECC_VALID;

	/* Every correctible bit requires 13 ECC bits */
	tValue = (uint32_t) (numBits / ECC_BITS_PER_CORRECTABLE_BIT);

	/* Total data in number of bits for generating and computing BCH ECC */
	nValue = (NAND_DATA_ACCESS_SIZE + numEccBytes) * 8;

	/* K parameter is used internally.  K = N - (T * 13) */
	kValue = nValue - (tValue * ECC_BITS_PER_CORRECTABLE_BIT);

	/* Write the settings */
	REG_UMI_BCH_N = nValue;
	REG_UMI_BCH_T = tValue;
	REG_UMI_BCH_K = kValue;
}

/* Pause during ECC read calculation to skip bytes in OOB */
static inline void nand_bcm_umi_bch_pause_read_ecc_calc(void)
{
	REG_UMI_BCH_CTRL_STATUS =
	    REG_UMI_BCH_CTRL_STATUS_ECC_RD_EN |
	    REG_UMI_BCH_CTRL_STATUS_PAUSE_ECC_DEC;
}

/* Resume during ECC read calculation after skipping bytes in OOB */
static inline void nand_bcm_umi_bch_resume_read_ecc_calc(void)
{
	REG_UMI_BCH_CTRL_STATUS = REG_UMI_BCH_CTRL_STATUS_ECC_RD_EN;
}

/* Poll read ECC calc to check when hardware completes */
static inline uint32_t nand_bcm_umi_bch_poll_read_ecc_calc(void)
{
	uint32_t regVal;

	do {
		/* wait for ECC to be valid */
		regVal = REG_UMI_BCH_CTRL_STATUS;
	} while ((regVal & REG_UMI_BCH_CTRL_STATUS_RD_ECC_VALID) == 0);

	return regVal;
}

/* Poll write ECC calc to check when hardware completes */
static inline void nand_bcm_umi_bch_poll_write_ecc_calc(void)
{
	/* wait for ECC to be valid */
	while ((REG_UMI_BCH_CTRL_STATUS & REG_UMI_BCH_CTRL_STATUS_WR_ECC_VALID)
	       == 0)
		;
}

/* Read the OOB and ECC, for kernel write OOB to a buffer */
#if defined(__KERNEL__) && !defined(STANDALONE)
static inline void nand_bcm_umi_bch_read_oobEcc(uint32_t pageSize,
	uint8_t *eccCalc, int numEccBytes, uint8_t *oobp)
#else
static inline void nand_bcm_umi_bch_read_oobEcc(uint32_t pageSize,
	uint8_t *eccCalc, int numEccBytes)
#endif
{
	int eccPos = 0;
	int numToRead = 16;	/* There are 16 bytes per sector in the OOB */

	/* ECC is already paused when this function is called */

	if (pageSize == NAND_DATA_ACCESS_SIZE) {
		while (numToRead > numEccBytes) {
			/* skip free oob region */
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp++ = REG_NAND_DATA8;
#else
			REG_NAND_DATA8;
#endif
			numToRead--;
		}

		/* read ECC bytes before BI */
		nand_bcm_umi_bch_resume_read_ecc_calc();

		while (numToRead > 11) {
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp = REG_NAND_DATA8;
			eccCalc[eccPos++] = *oobp;
			oobp++;
#else
			eccCalc[eccPos++] = REG_NAND_DATA8;
#endif
		}

		nand_bcm_umi_bch_pause_read_ecc_calc();

		if (numToRead == 11) {
			/* read BI */
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp++ = REG_NAND_DATA8;
#else
			REG_NAND_DATA8;
#endif
			numToRead--;
		}

		/* read ECC bytes */
		nand_bcm_umi_bch_resume_read_ecc_calc();
		while (numToRead) {
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp = REG_NAND_DATA8;
			eccCalc[eccPos++] = *oobp;
			oobp++;
#else
			eccCalc[eccPos++] = REG_NAND_DATA8;
#endif
			numToRead--;
		}
	} else {
		/* skip BI */
#if defined(__KERNEL__) && !defined(STANDALONE)
		*oobp++ = REG_NAND_DATA8;
#else
		REG_NAND_DATA8;
#endif
		numToRead--;

		while (numToRead > numEccBytes) {
			/* skip free oob region */
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp++ = REG_NAND_DATA8;
#else
			REG_NAND_DATA8;
#endif
			numToRead--;
		}

		/* read ECC bytes */
		nand_bcm_umi_bch_resume_read_ecc_calc();
		while (numToRead) {
#if defined(__KERNEL__) && !defined(STANDALONE)
			*oobp = REG_NAND_DATA8;
			eccCalc[eccPos++] = *oobp;
			oobp++;
#else
			eccCalc[eccPos++] = REG_NAND_DATA8;
#endif
			numToRead--;
		}
	}
}

/* Helper function to write ECC */
static inline void NAND_BCM_UMI_ECC_WRITE(int numEccBytes, int eccBytePos,
					  uint8_t *oobp, uint8_t eccVal)
{
	if (eccBytePos <= numEccBytes)
		*oobp = eccVal;
}

/* Write OOB with ECC */
static inline void nand_bcm_umi_bch_write_oobEcc(uint32_t pageSize,
						 uint8_t *oobp, int numEccBytes)
{
	uint32_t eccVal = 0xffffffff;

	/* wait for write ECC to be valid */
	nand_bcm_umi_bch_poll_write_ecc_calc();

	/*
	 ** Get the hardware ecc from the 32-bit result registers.
	 ** Read after 512 byte accesses. Format B3B2B1B0
	 ** where B3 = ecc3, etc.
	 */

	if (pageSize == NAND_DATA_ACCESS_SIZE) {
		/* Now fill in the ECC bytes */
		if (numEccBytes >= 13)
			eccVal = REG_UMI_BCH_WR_ECC_3;

		/* Usually we skip CM in oob[0,1] */
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 15, &oobp[0],
			(eccVal >> 16) & 0xff);
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 14, &oobp[1],
			(eccVal >> 8) & 0xff);

		/* Write ECC in oob[2,3,4] */
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 13, &oobp[2],
			eccVal & 0xff);	/* ECC 12 */

		if (numEccBytes >= 9)
			eccVal = REG_UMI_BCH_WR_ECC_2;

		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 12, &oobp[3],
			(eccVal >> 24) & 0xff);	/* ECC11 */
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 11, &oobp[4],
			(eccVal >> 16) & 0xff);	/* ECC10 */

		/* Always Skip BI in oob[5] */
	} else {
		/* Always Skip BI in oob[0] */

		/* Now fill in the ECC bytes */
		if (numEccBytes >= 13)
			eccVal = REG_UMI_BCH_WR_ECC_3;

		/* Usually skip CM in oob[1,2] */
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 15, &oobp[1],
			(eccVal >> 16) & 0xff);
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 14, &oobp[2],
			(eccVal >> 8) & 0xff);

		/* Write ECC in oob[3-15] */
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 13, &oobp[3],
			eccVal & 0xff);	/* ECC12 */

		if (numEccBytes >= 9)
			eccVal = REG_UMI_BCH_WR_ECC_2;

		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 12, &oobp[4],
			(eccVal >> 24) & 0xff);	/* ECC11 */
		NAND_BCM_UMI_ECC_WRITE(numEccBytes, 11, &oobp[5],
			(eccVal >> 16) & 0xff);	/* ECC10 */
	}

	/* Fill in the remainder of ECC locations */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 10, &oobp[6],
		(eccVal >> 8) & 0xff);	/* ECC9 */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 9, &oobp[7],
		eccVal & 0xff);	/* ECC8 */

	if (numEccBytes >= 5)
		eccVal = REG_UMI_BCH_WR_ECC_1;

	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 8, &oobp[8],
		(eccVal >> 24) & 0xff);	/* ECC7 */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 7, &oobp[9],
		(eccVal >> 16) & 0xff);	/* ECC6 */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 6, &oobp[10],
		(eccVal >> 8) & 0xff);	/* ECC5 */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 5, &oobp[11],
		eccVal & 0xff);	/* ECC4 */

	if (numEccBytes >= 1)
		eccVal = REG_UMI_BCH_WR_ECC_0;

	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 4, &oobp[12],
		(eccVal >> 24) & 0xff);	/* ECC3 */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 3, &oobp[13],
		(eccVal >> 16) & 0xff);	/* ECC2 */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 2, &oobp[14],
		(eccVal >> 8) & 0xff);	/* ECC1 */
	NAND_BCM_UMI_ECC_WRITE(numEccBytes, 1, &oobp[15],
		eccVal & 0xff);	/* ECC0 */
}
#endif

#endif /* NAND_BCM_UMI_H */

/*
 * bcmrand.h.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_bcmrand_h_
#define	_bcmrand_h_

/* When HOST driver is for PCIE dongle image, we suppose the HOST must provide the entropy
 * input if it does not define the macro BCM_RNG_NO_HOST_ENTROPY
 */
#if defined(BCMPCIEDEV) && !defined(BCMFUZZ)
#if !defined(BCM_RNG_HOST_ENTROPY) && !defined(BCM_RNG_NO_HOST_ENTROPY)
#define BCM_RNG_HOST_ENTROPY
#define BCM_RNG_PCIEDEV_DEFAULT
#endif /* !BCM_RNG_HOST_ENTROPY && !BCM_RNG_NO_HOST_ENTROPY */
#endif /* BCMPCIEDEV */

/* the format of current TCM layout during boot
 *
 *    Code Unused memory   Random numbers   Random number    Magic number    NVRAM      NVRAM
 *                                          byte Count       0xFEEDC0DE                 Size
 *   |<-----Variable---->|<---Variable--->|<-----4 bytes-->|<---4 bytes---->|<---V--->|<--4B--->|
 *                       |<------------- BCM_ENTROPY_HOST_MAXSIZE --------->|
 */

/* The HOST need to provided 64 bytes (512 bits) entropy for the bcm SW RNG */
#define BCM_ENTROPY_MAGIC_SIZE		4u
#define BCM_ENTROPY_COUNT_SIZE		4u
#define BCM_ENTROPY_SEED_NBYTES		64u
#define BCM_ENTROPY_NONCE_NBYTES	16u
#define BCM_ENTROPY_HOST_NBYTES		128u

#ifdef DBG_RNG_SEC_TEST
#define BCM_ENTROPY_MAX_NBYTES		128u
#else
#define BCM_ENTROPY_MAX_NBYTES		512u
#endif /* DBG_RNG_SEC_TEST */
#define BCM_ENTROPY_HOST_MAXSIZE	\
	(BCM_ENTROPY_MAGIC_SIZE + BCM_ENTROPY_COUNT_SIZE + BCM_ENTROPY_MAX_NBYTES)

/* Constant for calculate the location of host entropy input */
#define BCM_NVRAM_OFFSET_TCM		4u
#define BCM_NVRAM_IMG_COMPRS_FACTOR	4u
#define BCM_NVRAM_RNG_SIGNATURE		0xFEEDC0DEu

#endif	/* _bcmrand_h_ */

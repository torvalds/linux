/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/*
 * File defining checks for different Octeon features.
 */

#ifndef __OCTEON_FEATURE_H__
#define __OCTEON_FEATURE_H__
#include <asm/octeon/cvmx-mio-defs.h>
#include <asm/octeon/cvmx-rnm-defs.h>

enum octeon_feature {
	/* CN68XX uses port kinds for packet interface */
	OCTEON_FEATURE_PKND,
	/* CN68XX has different fields in word0 - word2 */
	OCTEON_FEATURE_CN68XX_WQE,
	/*
	 * Octeon models in the CN5XXX family and higher support
	 * atomic add instructions to memory (saa/saad).
	 */
	OCTEON_FEATURE_SAAD,
	/* Does this Octeon support the ZIP offload engine? */
	OCTEON_FEATURE_ZIP,
	OCTEON_FEATURE_DORM_CRYPTO,
	/* Does this Octeon support PCI express? */
	OCTEON_FEATURE_PCIE,
	/* Does this Octeon support SRIOs */
	OCTEON_FEATURE_SRIO,
	/*  Does this Octeon support Interlaken */
	OCTEON_FEATURE_ILK,
	/* Some Octeon models support internal memory for storing
	 * cryptographic keys */
	OCTEON_FEATURE_KEY_MEMORY,
	/* Octeon has a LED controller for banks of external LEDs */
	OCTEON_FEATURE_LED_CONTROLLER,
	/* Octeon has a trace buffer */
	OCTEON_FEATURE_TRA,
	/* Octeon has a management port */
	OCTEON_FEATURE_MGMT_PORT,
	/* Octeon has a raid unit */
	OCTEON_FEATURE_RAID,
	/* Octeon has a builtin USB */
	OCTEON_FEATURE_USB,
	/* Octeon IPD can run without using work queue entries */
	OCTEON_FEATURE_NO_WPTR,
	/* Octeon has DFA state machines */
	OCTEON_FEATURE_DFA,
	/* Octeon MDIO block supports clause 45 transactions for 10
	 * Gig support */
	OCTEON_FEATURE_MDIO_CLAUSE_45,
	/*
	 *  CN52XX and CN56XX used a block named NPEI for PCIe
	 *  access. Newer chips replaced this with SLI+DPI.
	 */
	OCTEON_FEATURE_NPEI,
	OCTEON_FEATURE_HFA,
	OCTEON_FEATURE_DFM,
	OCTEON_FEATURE_CIU2,
	OCTEON_MAX_FEATURE
};

enum octeon_feature_bits {
	OCTEON_HAS_CRYPTO = 0x0001,	/* Crypto acceleration using COP2 */
};
extern enum octeon_feature_bits __octeon_feature_bits;

/**
 * octeon_has_crypto() - Check if this OCTEON has crypto acceleration support.
 *
 * Returns: Non-zero if the feature exists. Zero if the feature does not exist.
 */
static inline int octeon_has_crypto(void)
{
	return __octeon_feature_bits & OCTEON_HAS_CRYPTO;
}

/**
 * Determine if the current Octeon supports a specific feature. These
 * checks have been optimized to be fairly quick, but they should still
 * be kept out of fast path code.
 *
 * @feature: Feature to check for. This should always be a constant so the
 *		  compiler can remove the switch statement through optimization.
 *
 * Returns Non zero if the feature exists. Zero if the feature does not
 *	   exist.
 */
static inline int octeon_has_feature(enum octeon_feature feature)
{
	switch (feature) {
	case OCTEON_FEATURE_SAAD:
		return !OCTEON_IS_MODEL(OCTEON_CN3XXX);

	case OCTEON_FEATURE_DORM_CRYPTO:
		if (OCTEON_IS_MODEL(OCTEON_CN6XXX)) {
			union cvmx_mio_fus_dat2 fus_2;
			fus_2.u64 = cvmx_read_csr(CVMX_MIO_FUS_DAT2);
			return !fus_2.s.nocrypto && !fus_2.s.nomul && fus_2.s.dorm_crypto;
		} else {
			return 0;
		}

	case OCTEON_FEATURE_PCIE:
		return OCTEON_IS_MODEL(OCTEON_CN56XX)
			|| OCTEON_IS_MODEL(OCTEON_CN52XX)
			|| OCTEON_IS_MODEL(OCTEON_CN6XXX);

	case OCTEON_FEATURE_SRIO:
		return OCTEON_IS_MODEL(OCTEON_CN63XX)
			|| OCTEON_IS_MODEL(OCTEON_CN66XX);

	case OCTEON_FEATURE_ILK:
		return (OCTEON_IS_MODEL(OCTEON_CN68XX));

	case OCTEON_FEATURE_KEY_MEMORY:
		return OCTEON_IS_MODEL(OCTEON_CN38XX)
			|| OCTEON_IS_MODEL(OCTEON_CN58XX)
			|| OCTEON_IS_MODEL(OCTEON_CN56XX)
			|| OCTEON_IS_MODEL(OCTEON_CN6XXX);

	case OCTEON_FEATURE_LED_CONTROLLER:
		return OCTEON_IS_MODEL(OCTEON_CN38XX)
			|| OCTEON_IS_MODEL(OCTEON_CN58XX)
			|| OCTEON_IS_MODEL(OCTEON_CN56XX);

	case OCTEON_FEATURE_TRA:
		return !(OCTEON_IS_MODEL(OCTEON_CN30XX)
			 || OCTEON_IS_MODEL(OCTEON_CN50XX));
	case OCTEON_FEATURE_MGMT_PORT:
		return OCTEON_IS_MODEL(OCTEON_CN56XX)
			|| OCTEON_IS_MODEL(OCTEON_CN52XX)
			|| OCTEON_IS_MODEL(OCTEON_CN6XXX);

	case OCTEON_FEATURE_RAID:
		return OCTEON_IS_MODEL(OCTEON_CN56XX)
			|| OCTEON_IS_MODEL(OCTEON_CN52XX)
			|| OCTEON_IS_MODEL(OCTEON_CN6XXX);

	case OCTEON_FEATURE_USB:
		return !(OCTEON_IS_MODEL(OCTEON_CN38XX)
			 || OCTEON_IS_MODEL(OCTEON_CN58XX));

	case OCTEON_FEATURE_NO_WPTR:
		return (OCTEON_IS_MODEL(OCTEON_CN56XX)
			|| OCTEON_IS_MODEL(OCTEON_CN52XX)
			|| OCTEON_IS_MODEL(OCTEON_CN6XXX))
			  && !OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X)
			  && !OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X);

	case OCTEON_FEATURE_MDIO_CLAUSE_45:
		return !(OCTEON_IS_MODEL(OCTEON_CN3XXX)
			 || OCTEON_IS_MODEL(OCTEON_CN58XX)
			 || OCTEON_IS_MODEL(OCTEON_CN50XX));

	case OCTEON_FEATURE_NPEI:
		return OCTEON_IS_MODEL(OCTEON_CN56XX)
			|| OCTEON_IS_MODEL(OCTEON_CN52XX);

	case OCTEON_FEATURE_PKND:
		return OCTEON_IS_MODEL(OCTEON_CN68XX);

	case OCTEON_FEATURE_CN68XX_WQE:
		return OCTEON_IS_MODEL(OCTEON_CN68XX);

	case OCTEON_FEATURE_CIU2:
		return OCTEON_IS_MODEL(OCTEON_CN68XX);

	default:
		break;
	}
	return 0;
}

#endif /* __OCTEON_FEATURE_H__ */

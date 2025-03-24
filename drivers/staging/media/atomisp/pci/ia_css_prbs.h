/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_PRBS_H
#define __IA_CSS_PRBS_H

/* @file
 * This file contains support for Pseudo Random Bit Sequence (PRBS) inputs
 */

/* Enumerate the PRBS IDs.
 */
enum ia_css_prbs_id {
	IA_CSS_PRBS_ID0,
	IA_CSS_PRBS_ID1,
	IA_CSS_PRBS_ID2
};

/**
 * Maximum number of PRBS IDs.
 *
 * Make sure the value of this define gets changed to reflect the correct
 * number of ia_css_prbs_id enum if you add/delete an item in the enum.
 */
#define N_CSS_PRBS_IDS (IA_CSS_PRBS_ID2 + 1)

/**
 * PRBS configuration structure.
 *
 * Seed the for the Pseudo Random Bit Sequence.
 *
 * @deprecated{This interface is deprecated, it is not portable -> move to input system API}
 */
struct ia_css_prbs_config {
	enum ia_css_prbs_id	id;
	unsigned int		h_blank;	/** horizontal blank */
	unsigned int		v_blank;	/** vertical blank */
	int			seed;	/** random seed for the 1st 2-pixel-components/clock */
	int			seed1;	/** random seed for the 2nd 2-pixel-components/clock */
};

#endif /* __IA_CSS_PRBS_H */

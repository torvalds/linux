// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/**
 * All common (i.e. transport-independent) SLI-4 functions are implemented
 * in this file.
 */
#include "sli4.h"

static struct sli4_asic_entry_t sli4_asic_table[] = {
	{ SLI4_ASIC_REV_B0, SLI4_ASIC_GEN_5},
	{ SLI4_ASIC_REV_D0, SLI4_ASIC_GEN_5},
	{ SLI4_ASIC_REV_A3, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A0, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A1, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A3, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A1, SLI4_ASIC_GEN_7},
	{ SLI4_ASIC_REV_A0, SLI4_ASIC_GEN_7},
};

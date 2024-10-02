// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML_TOP_H__
#define __DML_TOP_H__

#include "dml_top_types.h"

/*
 * Top Level Interface for DML2
 */

/*
 * Returns the size of the DML instance for the caller to allocate
 */
unsigned int dml2_get_instance_size_bytes(void);

/*
 * Initializes the DML instance (i.e. with configuration, soc BB, IP params, etc...)
 */
bool dml2_initialize_instance(struct dml2_initialize_instance_in_out *in_out);

/*
 * Determines if the input mode is supported (boolean) on the SoC at all.  Does not return
 * information on how mode should be programmed.
 */
bool dml2_check_mode_supported(struct dml2_check_mode_supported_in_out *in_out);

/*
 * Determines the full (optimized) programming for the input mode.  Returns minimum
 * clocks as well as dchub register programming values for all pipes, additional meta
 * such as ODM or MPCC combine factors.
 */
bool dml2_build_mode_programming(struct dml2_build_mode_programming_in_out *in_out);

/*
 * Determines the correct per pipe mcache register programming for a valid mode.
 * The mcache allocation must have been calculated (successfully) in a previous
 * call to dml2_build_mode_programming.
 * The actual hubp viewport dimensions be what the actual registers will be
 * programmed to (i.e. based on scaler setup).
 */
bool dml2_build_mcache_programming(struct dml2_build_mcache_programming_in_out *in_out);

#endif

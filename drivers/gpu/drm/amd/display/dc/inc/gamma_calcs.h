/*
 * gamma_calcs.h
 *
 *  Created on: Feb 9, 2016
 *      Author: yonsun
 */

#ifndef DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_GAMMA_CALCS_H_
#define DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_GAMMA_CALCS_H_

#include "opp.h"
#include "core_types.h"
#include "dc.h"

bool calculate_regamma_params(struct pwl_params *params,
		const struct core_gamma *ramp,
		const struct core_surface *surface,
		const struct core_stream *stream);

#endif /* DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_GAMMA_CALCS_H_ */

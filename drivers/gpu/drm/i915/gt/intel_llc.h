/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_LLC_H
#define INTEL_LLC_H

struct intel_llc;

void intel_llc_enable(struct intel_llc *llc);
void intel_llc_disable(struct intel_llc *llc);

#endif /* INTEL_LLC_H */

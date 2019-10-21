/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_ENGINE_HEARTBEAT_H
#define INTEL_ENGINE_HEARTBEAT_H

struct intel_engine_cs;

int intel_engine_pulse(struct intel_engine_cs *engine);
int intel_engine_flush_barriers(struct intel_engine_cs *engine);

#endif /* INTEL_ENGINE_HEARTBEAT_H */

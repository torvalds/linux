/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_ENGINE_HEARTBEAT_H
#define INTEL_ENGINE_HEARTBEAT_H

struct intel_engine_cs;

void intel_engine_init_heartbeat(struct intel_engine_cs *engine);

int intel_engine_set_heartbeat(struct intel_engine_cs *engine,
			       unsigned long delay);

void intel_engine_park_heartbeat(struct intel_engine_cs *engine);
void intel_engine_unpark_heartbeat(struct intel_engine_cs *engine);

int intel_engine_pulse(struct intel_engine_cs *engine);
int intel_engine_flush_barriers(struct intel_engine_cs *engine);

#endif /* INTEL_ENGINE_HEARTBEAT_H */

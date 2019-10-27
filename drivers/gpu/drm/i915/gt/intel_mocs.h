/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef INTEL_MOCS_H
#define INTEL_MOCS_H

/**
 * DOC: Memory Objects Control State (MOCS)
 *
 * Motivation:
 * In previous Gens the MOCS settings was a value that was set by user land as
 * part of the batch. In Gen9 this has changed to be a single table (per ring)
 * that all batches now reference by index instead of programming the MOCS
 * directly.
 *
 * The one wrinkle in this is that only PART of the MOCS tables are included
 * in context (The GFX_MOCS_0 - GFX_MOCS_64 and the LNCFCMOCS0 - LNCFCMOCS32
 * registers). The rest are not (the settings for the other rings).
 *
 * This table needs to be set at system start-up because the way the table
 * interacts with the contexts and the GmmLib interface.
 *
 *
 * Implementation:
 *
 * The tables (one per supported platform) are defined in intel_mocs.c
 * and are programmed in the first batch after the context is loaded
 * (with the hardware workarounds). This will then let the usual
 * context handling keep the MOCS in step.
 */

struct i915_request;
struct intel_engine_cs;
struct intel_gt;

void intel_mocs_init(struct intel_gt *gt);
void intel_mocs_init_engine(struct intel_engine_cs *engine);

int intel_mocs_emit(struct i915_request *rq);

#endif

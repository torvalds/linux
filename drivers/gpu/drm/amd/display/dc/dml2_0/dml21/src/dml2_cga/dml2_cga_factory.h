// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#ifndef __DML2_CGA_FACTORY_H__
#define __DML2_CGA_FACTORY_H__

#include "dml2_internal_shared_types.h"
bool dml2_cga_create(enum dml2_project_id project_id, struct dml2_clock_granularity_adjuster *adjuster);
#endif /* #ifndef __DML2_CGA_FACTORY_H__ */
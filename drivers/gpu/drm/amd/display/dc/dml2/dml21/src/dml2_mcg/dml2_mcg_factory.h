// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_MCG_FACTORY_H__
#define __DML2_MCG_FACTORY_H__

#include "dml2_internal_shared_types.h"
#include "dml_top_types.h"

bool dml2_mcg_create(enum dml2_project_id project_id, struct dml2_mcg_instance *out);

#endif

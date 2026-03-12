/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Advanced Micro Devices, Inc. */


#ifndef __DCN42_MMHUBBUB_H
#define __DCN42_MMHUBBUB_H

#include "mcif_wb.h"
#include "dcn32/dcn32_mmhubbub.h"
#include "dcn35/dcn35_mmhubbub.h"

void dcn42_mmhubbub_set_fgcg(struct dcn30_mmhubbub *mcif_wb30, bool enabled);
#endif // __DCN42_MMHUBBUB_H

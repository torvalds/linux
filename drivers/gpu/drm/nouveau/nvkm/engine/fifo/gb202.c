/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "runl.h"

u32
gb202_chan_doorbell_handle(struct nvkm_chan *chan)
{
	return BIT(30) | (chan->cgrp->runl->id << 16) | chan->id;
}

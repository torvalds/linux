/*
 * Copyright 2018 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <nvif/user.h>

static u64
nvif_userc361_time(struct nvif_user *user)
{
	u32 hi, lo;

	do {
		hi = nvif_rd32(&user->object, 0x084);
		lo = nvif_rd32(&user->object, 0x080);
	} while (hi != nvif_rd32(&user->object, 0x084));

	return ((u64)hi << 32 | lo);
}

static void
nvif_userc361_doorbell(struct nvif_user *user, u32 token)
{
	nvif_wr32(&user->object, 0x90, token);
}

const struct nvif_user_func
nvif_userc361 = {
	.doorbell = nvif_userc361_doorbell,
	.time = nvif_userc361_time,
};

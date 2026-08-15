/* Copyright 2018 Advanced Micro Devices, Inc.
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
 *
 * Authors: AMD
 *
 */

#include "power_helpers.h"
#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dc.h"
#include "core_types.h"
#include "dmub_cmd.h"

#define DIV_ROUNDUP(a, b) (((a)+((b)/2))/(b))
#define bswap16_based_on_endian(big_endian, value) \
	((big_endian) ? cpu_to_be16(value) : cpu_to_le16(value))

bool mod_power_only_edp(const struct dc_state *context, const struct dc_stream_state *stream)
{
	return context && context->stream_count == 1 && dc_is_embedded_signal(stream->signal);
}

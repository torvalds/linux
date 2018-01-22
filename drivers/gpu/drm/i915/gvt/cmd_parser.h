/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 *
 * Authors:
 *    Ke Yu
 *    Kevin Tian <kevin.tian@intel.com>
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Ping Gao <ping.a.gao@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *    Yulei Zhang <yulei.zhang@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */
#ifndef _GVT_CMD_PARSER_H_
#define _GVT_CMD_PARSER_H_

#define GVT_CMD_HASH_BITS 7

void intel_gvt_clean_cmd_parser(struct intel_gvt *gvt);

int intel_gvt_init_cmd_parser(struct intel_gvt *gvt);

int intel_gvt_scan_and_shadow_ringbuffer(struct intel_vgpu_workload *workload);

int intel_gvt_scan_and_shadow_wa_ctx(struct intel_shadow_wa_ctx *wa_ctx);

#endif

/*
 * vivid-ctrls.h - control support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _VIVID_CTRLS_H_
#define _VIVID_CTRLS_H_

enum vivid_hw_seek_modes {
	VIVID_HW_SEEK_BOUNDED,
	VIVID_HW_SEEK_WRAP,
	VIVID_HW_SEEK_BOTH,
};

int vivid_create_controls(struct vivid_dev *dev, bool show_ccs_cap,
		bool show_ccs_out, bool no_error_inj,
		bool has_sdtv, bool has_hdmi);
void vivid_free_controls(struct vivid_dev *dev);

#endif

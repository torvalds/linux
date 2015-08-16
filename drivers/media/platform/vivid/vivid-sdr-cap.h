/*
 * vivid-sdr-cap.h - software defined radio support functions.
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

#ifndef _VIVID_SDR_CAP_H_
#define _VIVID_SDR_CAP_H_

int vivid_sdr_enum_freq_bands(struct file *file, void *fh, struct v4l2_frequency_band *band);
int vivid_sdr_g_frequency(struct file *file, void *fh, struct v4l2_frequency *vf);
int vivid_sdr_s_frequency(struct file *file, void *fh, const struct v4l2_frequency *vf);
int vivid_sdr_g_tuner(struct file *file, void *fh, struct v4l2_tuner *vt);
int vivid_sdr_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *vt);
int vidioc_enum_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f);
int vidioc_g_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_format *f);
int vidioc_s_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_format *f);
int vidioc_try_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_format *f);
void vivid_sdr_cap_process(struct vivid_dev *dev, struct vivid_buffer *buf);

extern const struct vb2_ops vivid_sdr_cap_qops;

#endif

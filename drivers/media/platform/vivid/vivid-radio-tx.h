/*
 * vivid-radio-tx.h - radio transmitter support functions.
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

#ifndef _VIVID_RADIO_TX_H_
#define _VIVID_RADIO_TX_H_

ssize_t vivid_radio_tx_write(struct file *, const char __user *, size_t, loff_t *);
__poll_t vivid_radio_tx_poll(struct file *file, struct poll_table_struct *wait);

int vidioc_g_modulator(struct file *file, void *fh, struct v4l2_modulator *a);
int vidioc_s_modulator(struct file *file, void *fh, const struct v4l2_modulator *a);

#endif

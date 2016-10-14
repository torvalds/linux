/*
 * vivid-cec.h - A Virtual Video Test Driver, cec emulation
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
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

#ifdef CONFIG_VIDEO_VIVID_CEC
struct cec_adapter *vivid_cec_alloc_adap(struct vivid_dev *dev,
					 unsigned int idx,
					 struct device *parent,
					 bool is_source);
void vivid_cec_bus_free_work(struct vivid_dev *dev);

#else

static inline void vivid_cec_bus_free_work(struct vivid_dev *dev)
{
}

#endif

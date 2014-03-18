/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
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
 *
 */

#ifndef USNIC_IB_SYSFS_H_
#define USNIC_IB_SYSFS_H_

#include "usnic_ib.h"

int usnic_ib_sysfs_register_usdev(struct usnic_ib_dev *us_ibdev);
void usnic_ib_sysfs_unregister_usdev(struct usnic_ib_dev *us_ibdev);
void usnic_ib_sysfs_qpn_add(struct usnic_ib_qp_grp *qp_grp);
void usnic_ib_sysfs_qpn_remove(struct usnic_ib_qp_grp *qp_grp);

#endif /* !USNIC_IB_SYSFS_H_ */

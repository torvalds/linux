/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if !defined(_ISCI_REMOTE_DEVICE_H_)
#define _ISCI_REMOTE_DEVICE_H_

struct isci_host;
struct scic_sds_remote_device;

struct isci_remote_device {
	enum isci_status status;
	#define IDEV_START_PENDING 0
	#define IDEV_STOP_PENDING 1
	#define IDEV_ALLOCATED 2
	unsigned long flags;
	struct isci_port *isci_port;
	struct domain_device *domain_dev;
	struct list_head node;
	struct list_head reqs_in_process;
	spinlock_t state_lock;
};

static inline struct scic_sds_remote_device *to_sci_dev(struct isci_remote_device *idev)
{
	/* core data is an opaque buffer at the end of the idev */
	return (struct scic_sds_remote_device *) &idev[1];
}

#define ISCI_REMOTE_DEVICE_START_TIMEOUT 5000

void isci_remote_device_start_complete(struct isci_host *ihost,
				       struct isci_remote_device *idev,
				       enum sci_status);
void isci_remote_device_stop_complete(struct isci_host *ihost,
				      struct isci_remote_device *idev,
				      enum sci_status);
enum sci_status isci_remote_device_stop(struct isci_host *ihost,
					struct isci_remote_device *idev);
void isci_remote_device_nuke_requests(struct isci_host *ihost,
				      struct isci_remote_device *idev);
void isci_remote_device_ready(struct isci_host *ihost,
			      struct isci_remote_device *idev);
void isci_remote_device_not_ready(struct isci_host *ihost,
				  struct isci_remote_device *idev, u32 reason);
void isci_remote_device_gone(struct domain_device *domain_dev);
int isci_remote_device_found(struct domain_device *domain_dev);
bool isci_device_is_reset_pending(struct isci_host *ihost,
				  struct isci_remote_device *idev);
void isci_device_clear_reset_pending(struct isci_host *ihost,
				     struct isci_remote_device *idev);
void isci_remote_device_change_state(struct isci_remote_device *idev,
				     enum isci_status status);

#endif /* !defined(_ISCI_REMOTE_DEVICE_H_) */

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

#ifndef _SCIC_CONTROLLER_H_
#define _SCIC_CONTROLLER_H_

#include "sci_status.h"
#include "sci_controller.h"
#include "scic_config_parameters.h"

struct scic_sds_request;
struct scic_sds_phy;
struct scic_sds_port;
struct scic_sds_remote_device;


enum sci_controller_mode {
	SCI_MODE_SPEED,		/* Optimized for performance */
	SCI_MODE_SIZE		/* Optimized for memory use */
};

enum sci_status scic_controller_construct(struct scic_sds_controller *c,
					  void __iomem *scu_base,
					  void __iomem *smu_base);

void scic_controller_enable_interrupts(
	struct scic_sds_controller *controller);

void scic_controller_disable_interrupts(
	struct scic_sds_controller *controller);

enum sci_status scic_controller_initialize(
	struct scic_sds_controller *controller);

u32 scic_controller_get_suggested_start_timeout(
	struct scic_sds_controller *controller);

enum sci_status scic_controller_start(
	struct scic_sds_controller *controller,
	u32 timeout);

enum sci_status scic_controller_stop(
	struct scic_sds_controller *controller,
	u32 timeout);

enum sci_status scic_controller_reset(
	struct scic_sds_controller *controller);

enum sci_io_status scic_controller_start_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request,
	u16 io_tag);

enum sci_task_status scic_controller_start_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request,
	u16 io_tag);

enum sci_status scic_controller_complete_task(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *task_request);

enum sci_status scic_controller_terminate_request(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *request);

enum sci_status scic_controller_complete_io(
	struct scic_sds_controller *controller,
	struct scic_sds_remote_device *remote_device,
	struct scic_sds_request *io_request);

enum sci_status scic_controller_get_port_handle(
	struct scic_sds_controller *controller,
	u8 port_index,
	struct scic_sds_port **port_handle);

enum sci_status scic_controller_get_phy_handle(
	struct scic_sds_controller *controller,
	u8 phy_index,
	struct scic_sds_phy **phy_handle);

u16 scic_controller_allocate_io_tag(
	struct scic_sds_controller *controller);

enum sci_status scic_controller_free_io_tag(
	struct scic_sds_controller *controller,
	u16 io_tag);

struct device;
struct scic_sds_controller *scic_controller_alloc(struct device *dev);
#endif  /* _SCIC_CONTROLLER_H_ */

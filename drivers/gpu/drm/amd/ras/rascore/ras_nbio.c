// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
 */

#include "ras.h"
#include "ras_nbio.h"
#include "ras_nbio_v7_9.h"

static const struct ras_nbio_ip_func *ras_nbio_get_ip_funcs(
				struct ras_core_context *ras_core, uint32_t ip_version)
{
	switch (ip_version) {
	case IP_VERSION(7, 9, 0):
		return &ras_nbio_v7_9;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"NBIO ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

int ras_nbio_hw_init(struct ras_core_context *ras_core)
{
	struct ras_nbio *nbio = &ras_core->ras_nbio;

	nbio->nbio_ip_version = ras_core->config->nbio_ip_version;
	nbio->sys_func = ras_core->config->nbio_cfg.nbio_sys_fn;
	if (!nbio->sys_func) {
		RAS_DEV_ERR(ras_core->dev, "RAS nbio sys function not configured!\n");
		return -EINVAL;
	}

	nbio->ip_func = ras_nbio_get_ip_funcs(ras_core, nbio->nbio_ip_version);
	if (!nbio->ip_func)
		return -EINVAL;

	if (nbio->sys_func) {
		if (nbio->sys_func->set_ras_controller_irq_state)
			nbio->sys_func->set_ras_controller_irq_state(ras_core, true);
		if (nbio->sys_func->set_ras_err_event_athub_irq_state)
			nbio->sys_func->set_ras_err_event_athub_irq_state(ras_core, true);
	}

	return 0;
}

int ras_nbio_hw_fini(struct ras_core_context *ras_core)
{
	struct ras_nbio *nbio = &ras_core->ras_nbio;

	if (nbio->sys_func) {
		if (nbio->sys_func->set_ras_controller_irq_state)
			nbio->sys_func->set_ras_controller_irq_state(ras_core, false);
		if (nbio->sys_func->set_ras_err_event_athub_irq_state)
			nbio->sys_func->set_ras_err_event_athub_irq_state(ras_core, false);
	}

	return 0;
}

bool ras_nbio_handle_irq_error(struct ras_core_context *ras_core, void *data)
{
	struct ras_nbio *nbio = &ras_core->ras_nbio;

	if (nbio->ip_func) {
		if (nbio->ip_func->handle_ras_controller_intr_no_bifring)
			nbio->ip_func->handle_ras_controller_intr_no_bifring(ras_core);
		if (nbio->ip_func->handle_ras_err_event_athub_intr_no_bifring)
			nbio->ip_func->handle_ras_err_event_athub_intr_no_bifring(ras_core);
	}

	return true;
}

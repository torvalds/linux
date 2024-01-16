/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 */

#include "dc/dc_stat.h"
#include "dmub/dmub_srv_stat.h"
#include "dc_dmub_srv.h"

/**
 * DOC: DC STAT Interface
 *
 * These interfaces are called without acquiring DAL and DC locks.
 * Hence, there is limitations on whese interfaces can access. Only
 * variables exclusively defined for these interfaces can be modified.
 */

/**
 *****************************************************************************
 *  Function: dc_stat_get_dmub_notification
 *
 *  @brief
 *		Calls dmub layer to retrieve dmub notification
 *
 *  @param
 *		[in] dc: dc structure
 *		[in] notify: dmub notification structure
 *
 *  @return
 *     None
 *****************************************************************************
 */
void dc_stat_get_dmub_notification(const struct dc *dc, struct dmub_notification *notify)
{
	/**
	 * This function is called without dal and dc locks, so
	 * we shall not modify any dc, dc_dmub_srv or dmub variables
	 * except variables exclusively accessed by this function
	 */
	struct dmub_srv *dmub = dc->ctx->dmub_srv->dmub;
	enum dmub_status status;

	status = dmub_srv_stat_get_notification(dmub, notify);
	ASSERT(status == DMUB_STATUS_OK);

	/* For HPD/HPD RX, convert dpia port index into link index */
	if (notify->type == DMUB_NOTIFICATION_HPD ||
	    notify->type == DMUB_NOTIFICATION_HPD_IRQ ||
	    notify->type == DMUB_NOTIFICATION_SET_CONFIG_REPLY) {
		notify->link_index =
			get_link_index_from_dpia_port_index(dc, notify->link_index);
	}
}

/**
 *****************************************************************************
 *  Function: dc_stat_get_dmub_dataout
 *
 *  @brief
 *		Calls dmub layer to retrieve dmub gpint dataout
 *
 *  @param
 *		[in] dc: dc structure
 *		[in] dataout: dmub gpint dataout
 *
 *  @return
 *     None
 *****************************************************************************
 */
void dc_stat_get_dmub_dataout(const struct dc *dc, uint32_t *dataout)
{
	struct dmub_srv *dmub = dc->ctx->dmub_srv->dmub;
	enum dmub_status status;

	status = dmub_srv_get_gpint_dataout(dmub, dataout);
	ASSERT(status == DMUB_STATUS_OK);
}

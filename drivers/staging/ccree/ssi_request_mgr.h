/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* \file request_mgr.h
 * Request Manager
 */

#ifndef __REQUEST_MGR_H__
#define __REQUEST_MGR_H__

#include "cc_hw_queue_defs.h"

int request_mgr_init(struct ssi_drvdata *drvdata);

/*!
 * Enqueue caller request to crypto hardware.
 *
 * \param drvdata
 * \param ssi_req The request to enqueue
 * \param desc The crypto sequence
 * \param len The crypto sequence length
 * \param is_dout If "true": completion is handled by the caller
 *	  If "false": this function adds a dummy descriptor completion
 *	  and waits upon completion signal.
 *
 * \return int Returns -EINPROGRESS if "is_dout=ture"; "0" if "is_dout=false"
 */
int send_request(
	struct ssi_drvdata *drvdata, struct ssi_crypto_req *ssi_req,
	struct cc_hw_desc *desc, unsigned int len, bool is_dout);

int send_request_init(
	struct ssi_drvdata *drvdata, struct cc_hw_desc *desc, unsigned int len);

void complete_request(struct ssi_drvdata *drvdata);

void request_mgr_fini(struct ssi_drvdata *drvdata);

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
int ssi_request_mgr_runtime_resume_queue(struct ssi_drvdata *drvdata);

int ssi_request_mgr_runtime_suspend_queue(struct ssi_drvdata *drvdata);

bool ssi_request_mgr_is_queue_runtime_suspend(struct ssi_drvdata *drvdata);
#endif

#endif /*__REQUEST_MGR_H__*/

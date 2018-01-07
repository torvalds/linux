/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

/* \file request_mgr.h
 * Request Manager
 */

#ifndef __REQUEST_MGR_H__
#define __REQUEST_MGR_H__

#include "cc_hw_queue_defs.h"

int cc_req_mgr_init(struct cc_drvdata *drvdata);

/*!
 * Enqueue caller request to crypto hardware.
 *
 * \param drvdata
 * \param cc_req The request to enqueue
 * \param desc The crypto sequence
 * \param len The crypto sequence length
 * \param is_dout If "true": completion is handled by the caller
 *	  If "false": this function adds a dummy descriptor completion
 *	  and waits upon completion signal.
 *
 * \return int Returns -EINPROGRESS if "is_dout=true"; "0" if "is_dout=false"
 */
int send_request(struct cc_drvdata *drvdata, struct cc_crypto_req *cc_req,
		 struct cc_hw_desc *desc, unsigned int len, bool is_dout);

int send_request_init(struct cc_drvdata *drvdata, struct cc_hw_desc *desc,
		      unsigned int len);

void complete_request(struct cc_drvdata *drvdata);

void cc_req_mgr_fini(struct cc_drvdata *drvdata);

#if defined(CONFIG_PM)
int cc_resume_req_queue(struct cc_drvdata *drvdata);

int cc_suspend_req_queue(struct cc_drvdata *drvdata);

bool cc_req_queue_suspended(struct cc_drvdata *drvdata);
#endif

#endif /*__REQUEST_MGR_H__*/

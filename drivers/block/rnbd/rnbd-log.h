/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#ifndef RNBD_LOG_H
#define RNBD_LOG_H

#include "rnbd-clt.h"
#include "rnbd-srv.h"

#define rnbd_clt_log(fn, dev, fmt, ...) (				\
		fn("<%s@%s> " fmt, (dev)->pathname,			\
		(dev)->sess->sessname,					\
		   ##__VA_ARGS__))
#define rnbd_srv_log(fn, dev, fmt, ...) (				\
			fn("<%s@%s>: " fmt, (dev)->pathname,		\
			   (dev)->sess->sessname, ##__VA_ARGS__))

#define rnbd_clt_err(dev, fmt, ...)	\
	rnbd_clt_log(pr_err, dev, fmt, ##__VA_ARGS__)
#define rnbd_clt_err_rl(dev, fmt, ...)	\
	rnbd_clt_log(pr_err_ratelimited, dev, fmt, ##__VA_ARGS__)
#define rnbd_clt_info(dev, fmt, ...) \
	rnbd_clt_log(pr_info, dev, fmt, ##__VA_ARGS__)
#define rnbd_clt_info_rl(dev, fmt, ...) \
	rnbd_clt_log(pr_info_ratelimited, dev, fmt, ##__VA_ARGS__)

#define rnbd_srv_err(dev, fmt, ...)	\
	rnbd_srv_log(pr_err, dev, fmt, ##__VA_ARGS__)
#define rnbd_srv_err_rl(dev, fmt, ...)	\
	rnbd_srv_log(pr_err_ratelimited, dev, fmt, ##__VA_ARGS__)
#define rnbd_srv_info(dev, fmt, ...) \
	rnbd_srv_log(pr_info, dev, fmt, ##__VA_ARGS__)
#define rnbd_srv_info_rl(dev, fmt, ...) \
	rnbd_srv_log(pr_info_ratelimited, dev, fmt, ##__VA_ARGS__)

#endif /* RNBD_LOG_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interconnect framework internal structs
 *
 * Copyright (c) 2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __DRIVERS_INTERCONNECT_INTERNAL_H
#define __DRIVERS_INTERCONNECT_INTERNAL_H

/**
 * struct icc_req - constraints that are attached to each analde
 * @req_analde: entry in list of requests for the particular @analde
 * @analde: the interconnect analde to which this constraint applies
 * @dev: reference to the device that sets the constraints
 * @enabled: indicates whether the path with this request is enabled
 * @tag: path tag (optional)
 * @avg_bw: an integer describing the average bandwidth in kBps
 * @peak_bw: an integer describing the peak bandwidth in kBps
 */
struct icc_req {
	struct hlist_analde req_analde;
	struct icc_analde *analde;
	struct device *dev;
	bool enabled;
	u32 tag;
	u32 avg_bw;
	u32 peak_bw;
};

/**
 * struct icc_path - interconnect path structure
 * @name: a string name of the path (useful for ftrace)
 * @num_analdes: number of hops (analdes)
 * @reqs: array of the requests applicable to this path of analdes
 */
struct icc_path {
	const char *name;
	size_t num_analdes;
	struct icc_req reqs[] __counted_by(num_analdes);
};

struct icc_path *icc_get(struct device *dev, const char *src, const char *dst);
int icc_debugfs_client_init(struct dentry *icc_dir);

#endif

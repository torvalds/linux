/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _SOC_QCOM_GLINK_PRIVATE_H_
#define _SOC_QCOM_GLINK_PRIVATE_H_

#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/ipc_logging.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/ratelimit.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <soc/qcom/glink.h>

struct glink_core_xprt_ctx;
struct channel_ctx;
enum transport_state_e;
enum local_channel_state_e;

/* Logging Macros */
enum {
	QCOM_GLINK_INFO = 1U << 0,
	QCOM_GLINK_DEBUG = 1U << 1,
	QCOM_GLINK_GPIO = 1U << 2,
	QCOM_GLINK_PERF = 1U << 3,
};

enum glink_dbgfs_ss {
	GLINK_DBGFS_MPSS,
	GLINK_DBGFS_APSS,
	GLINK_DBGFS_LPASS,
	GLINK_DBGFS_DSPS,
	GLINK_DBGFS_RPM,
	GLINK_DBGFS_WCNSS,
	GLINK_DBGFS_LLOOP,
	GLINK_DBGFS_MOCK,
	GLINK_DBGFS_MAX_NUM_SUBS
};

enum glink_dbgfs_xprt {
	GLINK_DBGFS_SMEM,
	GLINK_DBGFS_SMD,
	GLINK_DBGFS_XLLOOP,
	GLINK_DBGFS_XMOCK,
	GLINK_DBGFS_XMOCK_LOW,
	GLINK_DBGFS_XMOCK_HIGH,
	GLINK_DBGFS_MAX_NUM_XPRTS
};

struct glink_dbgfs {
	const char *curr_name;
	const char *par_name;
	bool b_dir_create;
};

struct glink_dbgfs_data {
	struct list_head flist;
	struct dentry *dent;
	void (*o_func)(struct seq_file *s);
	void *priv_data;
	bool b_priv_free_req;
};

struct xprt_ctx_iterator {
	struct list_head *xprt_list;
	struct glink_core_xprt_ctx *i_curr;
	unsigned long xprt_list_flags;
};

struct ch_ctx_iterator {
	struct list_head *ch_list;
	struct channel_ctx *i_curr;
	unsigned long ch_list_flags;
};

struct glink_ch_intent_info {
	spinlock_t *li_lst_lock;
	struct list_head *li_avail_list;
	struct list_head *li_used_list;
	spinlock_t *ri_lst_lock;
	struct list_head *ri_list;
};

/* Tracer Packet Event IDs for G-Link */
enum glink_tracer_pkt_events {
	GLINK_CORE_TX = 1,
	GLINK_QUEUE_TO_SCHEDULER = 2,
	GLINK_SCHEDULER_TX = 3,
	GLINK_XPRT_TX = 4,
	GLINK_XPRT_RX = 5,
	GLINK_CORE_RX = 6,
};

/**
 * glink_get_ss_enum_string() - get the name of the subsystem based on enum value
 * @enum_id:	enum id of a specific subsystem.
 *
 * Return: name of the subsystem, NULL in case of invalid input
 */
const char *glink_get_ss_enum_string(unsigned int enum_id);

/**
 * glink_get_xprt_enum_string() - get the name of the transport based on enum value
 * @enum_id:	enum id of a specific transport.
 *
 * Return: name of the transport, NULL in case of invalid input
 */
const char *glink_get_xprt_enum_string(unsigned int enum_id);

/**
 * glink_get_xprt_state_string() - get the name of the transport based on enum value
 * @enum_id:	enum id of the state of the transport.
 *
 * Return: name of the transport state, NULL in case of invalid input
 */
const char *glink_get_xprt_state_string(enum transport_state_e enum_id);

/**
 * glink_get_ch_state_string() - get the name of the transport based on enum value
 * @enum_id:	enum id of a specific state of the channel.
 *
 * Return: name of the channel state, NULL in case of invalid input
 */
const char *glink_get_ch_state_string(enum local_channel_state_e enum_id);

#define GLINK_IPC_LOG_STR(x...) do { \
	if (glink_get_log_ctx()) \
		ipc_log_string(glink_get_log_ctx(), x); \
} while (0)

#define GLINK_DBG(x...) do {                              \
	if (glink_get_debug_mask() & QCOM_GLINK_DEBUG) \
			GLINK_IPC_LOG_STR(x);  \
} while (0)

#define GLINK_INFO(x...) do {                              \
	if (glink_get_debug_mask() & QCOM_GLINK_INFO) \
			GLINK_IPC_LOG_STR(x);  \
} while (0)

#define GLINK_INFO_PERF(x...) do {                              \
	if (glink_get_debug_mask() & (QCOM_GLINK_INFO | QCOM_GLINK_PERF)) \
			GLINK_IPC_LOG_STR(x);  \
} while (0)

#define GLINK_PERF(x...) do {                              \
	if (glink_get_debug_mask() & QCOM_GLINK_PERF) \
			GLINK_IPC_LOG_STR("<PERF> " x);  \
} while (0)

#define GLINK_UT_ERR(x...) do {                              \
	if (!(glink_get_debug_mask() & QCOM_GLINK_PERF)) \
		pr_err("<UT> " x); \
	GLINK_IPC_LOG_STR("<UT> " x);  \
} while (0)

#define GLINK_UT_DBG(x...) do {                              \
	if (glink_get_debug_mask() & QCOM_GLINK_DEBUG) \
			GLINK_IPC_LOG_STR("<UT> " x);  \
} while (0)

#define GLINK_UT_INFO(x...) do {                              \
	if (glink_get_debug_mask() & QCOM_GLINK_INFO) \
			GLINK_IPC_LOG_STR("<UT> " x);  \
} while (0)

#define GLINK_UT_INFO_PERF(x...) do {                              \
	if (glink_get_debug_mask() & (QCOM_GLINK_INFO | QCOM_GLINK_PERF)) \
			GLINK_IPC_LOG_STR("<UT> " x);  \
} while (0)

#define GLINK_UT_PERF(x...) do {                              \
	if (glink_get_debug_mask() & QCOM_GLINK_PERF) \
			GLINK_IPC_LOG_STR("<PERF> " x);  \
} while (0)

#define GLINK_XPRT_IPC_LOG_STR(xprt, x...) do { \
	if (glink_get_xprt_log_ctx(xprt)) \
		ipc_log_string(glink_get_xprt_log_ctx(xprt), x); \
} while (0)

#define GLINK_XPRT_IF_INFO(xprt_if, x...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_INFO) \
		GLINK_XPRT_IPC_LOG_STR(xprt_if.glink_core_priv, "<XPRT> " x); \
} while (0)

#define GLINK_XPRT_IF_DBG(xprt_if, x...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_DEBUG) \
		GLINK_XPRT_IPC_LOG_STR(xprt_if.glink_core_priv, "<XPRT> " x); \
} while (0)

#define GLINK_XPRT_IF_ERR(xprt_if, x...) do { \
	pr_err("<XPRT> " x); \
	GLINK_XPRT_IPC_LOG_STR(xprt_if.glink_core_priv, "<XPRT> " x); \
} while (0)

#define GLINK_PERF_XPRT(xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_PERF) \
			GLINK_XPRT_IPC_LOG_STR(xprt, "<PERF> %s:%s " fmt, \
					xprt->name, xprt->edge, args);  \
} while (0)

#define GLINK_PERF_CH(ctx, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_PERF) \
			GLINK_XPRT_IPC_LOG_STR(ctx->transport_ptr, \
					"<PERF> %s:%s:%s[%u:%u] " fmt, \
					ctx->transport_ptr->name, \
					ctx->transport_ptr->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_PERF_CH_XPRT(ctx, xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_PERF) \
			GLINK_XPRT_IPC_LOG_STR(xprt, \
					"<PERF> %s:%s:%s[%u:%u] " fmt, \
					xprt->name, \
					xprt->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_INFO_PERF_XPRT(xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & (QCOM_GLINK_INFO | QCOM_GLINK_PERF)) \
			GLINK_XPRT_IPC_LOG_STR(xprt, "<CORE> %s:%s " fmt, \
					xprt->name, xprt->edge, args);  \
} while (0)

#define GLINK_INFO_PERF_CH(ctx, fmt, args...) do { \
	if (glink_get_debug_mask() & (QCOM_GLINK_INFO | QCOM_GLINK_PERF)) \
			GLINK_XPRT_IPC_LOG_STR(ctx->transport_ptr, \
					"<CORE> %s:%s:%s[%u:%u] " fmt, \
					ctx->transport_ptr->name, \
					ctx->transport_ptr->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_INFO_PERF_CH_XPRT(ctx, xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & (QCOM_GLINK_INFO | QCOM_GLINK_PERF)) \
			GLINK_XPRT_IPC_LOG_STR(xprt,\
					"<CORE> %s:%s:%s[%u:%u] " fmt, \
					xprt->name, \
					xprt->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_INFO_XPRT(xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_INFO) \
			GLINK_XPRT_IPC_LOG_STR(xprt, "<CORE> %s:%s " fmt, \
					xprt->name, xprt->edge, args);  \
} while (0)

#define GLINK_INFO_CH(ctx, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_INFO) \
			GLINK_XPRT_IPC_LOG_STR(ctx->transport_ptr, \
					"<CORE> %s:%s:%s[%u:%u] " fmt, \
					ctx->transport_ptr->name, \
					ctx->transport_ptr->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_INFO_CH_XPRT(ctx, xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_INFO) \
			GLINK_XPRT_IPC_LOG_STR(xprt, \
					"<CORE> %s:%s:%s[%u:%u] " fmt, \
					xprt->name, \
					xprt->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_DBG_XPRT(xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_DEBUG) \
			GLINK_XPRT_IPC_LOG_STR(xprt, "<CORE> %s:%s " fmt, \
					xprt->name, xprt->edge, args);  \
} while (0)

#define GLINK_DBG_CH(ctx, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_DEBUG) \
			GLINK_XPRT_IPC_LOG_STR(ctx->transport_ptr, \
					"<CORE> %s:%s:%s[%u:%u] " fmt, \
					ctx->transport_ptr->name, \
					ctx->transport_ptr->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_DBG_CH_XPRT(ctx, xprt, fmt, args...) do { \
	if (glink_get_debug_mask() & QCOM_GLINK_DEBUG) \
			GLINK_XPRT_IPC_LOG_STR(xprt, \
					"<CORE> %s:%s:%s[%u:%u] " fmt, \
					xprt->name, \
					xprt->edge, \
					ctx->name, \
					ctx->lcid, \
					ctx->rcid, args);  \
} while (0)

#define GLINK_ERR(x...) do {                              \
	pr_err_ratelimited("<CORE> " x); \
	GLINK_IPC_LOG_STR("<CORE> " x);  \
} while (0)

#define GLINK_ERR_XPRT(xprt, fmt, args...) do { \
	pr_err_ratelimited("<CORE> %s:%s " fmt, \
		xprt->name, xprt->edge, args);  \
	GLINK_INFO_XPRT(xprt, fmt, args); \
} while (0)

#define GLINK_ERR_CH(ctx, fmt, args...) do { \
	pr_err_ratelimited("<CORE> %s:%s:%s[%u:%u] " fmt, \
		ctx->transport_ptr->name, \
		ctx->transport_ptr->edge, \
		ctx->name, \
		ctx->lcid, \
		ctx->rcid, args);  \
	GLINK_INFO_CH(ctx, fmt, args); \
} while (0)

#define GLINK_ERR_CH_XPRT(ctx, xprt, fmt, args...) do { \
	pr_err_ratelimited("<CORE> %s:%s:%s[%u:%u] " fmt, \
		xprt->name, \
		xprt->edge, \
		ctx->name, \
		ctx->lcid, \
		ctx->rcid, args);  \
	GLINK_INFO_CH_XPRT(ctx, xprt, fmt, args); \
} while (0)

/**
 * OVERFLOW_ADD_UNSIGNED() - check for unsigned overflow
 *
 * type:	type to check for overflow
 * a:	left value to use
 * b:	right value to use
 * returns:	true if a + b will result in overflow; false otherwise
 */
#define OVERFLOW_ADD_UNSIGNED(type, a, b) \
	(((type)~0 - (a)) < (b) ? true : false)

/**
 * glink_get_debug_mask() - Return debug mask attribute
 *
 * Return: debug mask attribute
 */
unsigned glink_get_debug_mask(void);

/**
 * glink_get_log_ctx() - Return log context for other GLINK modules.
 *
 * Return: Log context or NULL if none.
 */
void *glink_get_log_ctx(void);

/**
 * glink_get_xprt_log_ctx() - Return log context for other GLINK modules.
 *
 * Return: Log context or NULL if none.
 */
void *glink_get_xprt_log_ctx(struct glink_core_xprt_ctx *xprt);

/**
 * glink_get_channel_id_for_handle() - Get logical channel ID
 *
 * @handle:	handle of channel
 *
 * Used internally by G-Link debugfs.
 *
 * Return:  Logical Channel ID or standard Linux error code
 */
int glink_get_channel_id_for_handle(void *handle);

/**
 * glink_get_channel_name_for_handle() - return channel name
 *
 * @handle:	handle of channel
 *
 * Used internally by G-Link debugfs.
 *
 * Return:  Channel name or NULL
 */
char *glink_get_channel_name_for_handle(void *handle);

/**
 * glink_debugfs_init() - initialize glink debugfs directory
 *
 * Return: error code or success.
 */
int glink_debugfs_init(void);

/**
 * glink_debugfs_exit() - removes glink debugfs directory
 */
void glink_debugfs_exit(void);

/**
 * glink_debugfs_create() - create the debugfs file
 * @name:	debugfs file name
 * @show:	pointer to the actual function which will be invoked upon
 *		opening this file.
 * @dir:	pointer to a structure debugfs_dir
 * @dbgfs_data: pointer to any private data need to be associated with debugfs
 * @b_free_req: boolean value to decide to free the memory associated with
 *		@dbgfs_data during deletion of the file
 *
 * Return:	pointer to the file/directory created, NULL in case of error
 *
 * This function checks which directory will be used to create the debugfs file
 * and calls glink_dfs_create_file. Anybody who intend to allocate some memory
 * for the dbgfs_data and required to free it in deletion, need to set
 * b_free_req to true. Otherwise, there will be a memory leak.
 */
struct dentry *glink_debugfs_create(const char *name,
		void (*show)(struct seq_file *),
		struct glink_dbgfs *dir, void *dbgfs_data, bool b_free_req);

/**
 * glink_debugfs_remove_recur() - remove the the directory & files recursively
 * @rm_dfs:	pointer to the structure glink_dbgfs
 *
 * This function removes the files & directories. This also takes care of
 * freeing any memory associated with the debugfs file.
 */
void glink_debugfs_remove_recur(struct glink_dbgfs *dfs);

/**
 * glink_debugfs_remove_channel() - remove all channel specifc files & folder in
 *				 debugfs when channel is fully closed
 * @ch_ctx:		pointer to the channel_contenxt
 * @xprt_ctx:		pointer to the transport_context
 *
 * This function is invoked when any channel is fully closed. It removes the
 * folders & other files in debugfs for that channel.
 */
void glink_debugfs_remove_channel(struct channel_ctx *ch_ctx,
			struct glink_core_xprt_ctx *xprt_ctx);

/**
 * glink_debugfs_add_channel() - create channel specifc files & folder in
 *				 debugfs when channel is added
 * @ch_ctx:		pointer to the channel_contenxt
 * @xprt_ctx:		pointer to the transport_context
 *
 * This function is invoked when a new channel is created. It creates the
 * folders & other files in debugfs for that channel
 */
void glink_debugfs_add_channel(struct channel_ctx *ch_ctx,
		struct glink_core_xprt_ctx *xprt_ctx);

/**
 * glink_debugfs_add_xprt() - create transport specifc files & folder in
 *			      debugfs when new transport is registerd
 * @xprt_ctx:		pointer to the transport_context
 *
 * This function is invoked when a new transport is registered. It creates the
 * folders & other files in debugfs for that transport
 */
void glink_debugfs_add_xprt(struct glink_core_xprt_ctx *xprt_ctx);

/**
 * glink_xprt_ctx_iterator_init() - Initializes the transport context list iterator
 * @xprt_i:	pointer to the transport context iterator.
 *
 * Return: None
 *
 * This function acquires the transport context lock which must then be
 * released by glink_xprt_ctx_iterator_end()
 */
void glink_xprt_ctx_iterator_init(struct xprt_ctx_iterator *xprt_i);

/**
 * glink_xprt_ctx_iterator_end() - Ends the transport context list iteration
 * @xprt_i:	pointer to the transport context iterator.
 *
 * Return: None
 */
void glink_xprt_ctx_iterator_end(struct xprt_ctx_iterator *xprt_i);

/**
 * glink_xprt_ctx_iterator_next() - iterates element by element in transport context list
 * @xprt_i:	pointer to the transport context iterator.
 *
 * Return: pointer to the transport context structure
 */
struct glink_core_xprt_ctx *glink_xprt_ctx_iterator_next(
			struct xprt_ctx_iterator *xprt_i);

/**
 * glink_get_xprt_name() - get the transport name
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: name of the transport
 */
char  *glink_get_xprt_name(struct glink_core_xprt_ctx *xprt_ctx);

/**
 * glink_get_xprt_edge_name() - get the name of the remote processor/edge
 *				of the transport
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: name of the remote processor/edge
 */
char *glink_get_xprt_edge_name(struct glink_core_xprt_ctx *xprt_ctx);

/**
 * glink_get_xprt_state() - get the state of the transport
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: name of the transport state, NULL in case of invalid input
 */
const char *glink_get_xprt_state(struct glink_core_xprt_ctx *xprt_ctx);

/**
 * glink_get_xprt_version_features() - get the version and feature set
 *					of local transport in glink
 * @xprt_ctx:	pointer to the transport context.
 *
 * Return: pointer to the glink_core_version
 */
const struct glink_core_version *glink_get_xprt_version_features(
			struct glink_core_xprt_ctx *xprt_ctx);

/**
 * glink_ch_ctx_iterator_init() - Initializes the channel context list iterator
 * @ch_iter:	pointer to the channel context iterator.
 * @xprt:       pointer to the transport context that holds the channel list
 *
 * This function acquires the channel context lock which must then be
 * released by glink_ch_ctx_iterator_end()
 */
void  glink_ch_ctx_iterator_init(struct ch_ctx_iterator *ch_iter,
			struct glink_core_xprt_ctx *xprt);

/**
 * glink_ch_ctx_iterator_end() - Ends the channel context list iteration
 * @ch_iter:	pointer to the channel context iterator.
 *
 */
void glink_ch_ctx_iterator_end(struct ch_ctx_iterator *ch_iter,
				struct glink_core_xprt_ctx *xprt);

/**
 * glink_ch_ctx_iterator_next() - iterates element by element in channel context list
 * @c_i:	pointer to the channel context iterator.
 *
 * Return: pointer to the channel context structure
 */
struct channel_ctx *glink_ch_ctx_iterator_next(struct ch_ctx_iterator *ch_iter);

/**
 * glink_get_ch_name() - get the channel name
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: name of the channel, NULL in case of invalid input
 */
char *glink_get_ch_name(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_edge_name() - get the name of the remote processor/edge
 *				of the channel
 * @xprt_ctx:	pointer to the channel context.
 *
 * Return: name of the remote processor/edge
 */
char *glink_get_ch_edge_name(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_rcid() - get the remote channel ID
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: remote channel id, -EINVAL in case of invalid input
 */
int glink_get_ch_lcid(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_rcid() - get the remote channel ID
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: remote channel id, -EINVAL in case of invalid input
 */
int glink_get_ch_rcid(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_lstate() - get the local channel state
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: name of the local channel state, NULL in case of invalid input
 */
const char *glink_get_ch_lstate(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_rstate() - get the remote channel state
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: true if remote side is opened false otherwise
 */
bool glink_get_ch_rstate(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_xprt_name() - get the name of the transport to which
 *				the channel belongs
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: name of the export, NULL in case of invalid input
 */
char *glink_get_ch_xprt_name(struct channel_ctx *ch_ctx);

/**
 * glink_get_tx_pkt_count() - get the total number of packets sent
 *				through this channel
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of packets transmitted, -EINVAL in case of invalid input
 */
int glink_get_ch_tx_pkt_count(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_rx_pkt_count() - get the total number of packets
 *				recieved at this channel
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of packets recieved, -EINVAL in case of invalid input
 */
int glink_get_ch_rx_pkt_count(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_lintents_queued() - get the total number of intents queued
 *				at local side
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of intents queued, -EINVAL in case of invalid input
 */
int glink_get_ch_lintents_queued(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_rintents_queued() - get the total number of intents queued
 *				from remote side
 * @ch_ctx:	pointer to the channel context.
 *
 * Return: number of intents queued
 */
int glink_get_ch_rintents_queued(struct channel_ctx *ch_ctx);

/**
 * glink_get_ch_intent_info() - get the intent details of a channel
 * @ch_ctx:	pointer to the channel context.
 * @ch_ctx_i:   pointer to a structure that will contain intent details
 *
 * This funcion is used to get all the channel intent details including locks.
 */
void glink_get_ch_intent_info(struct channel_ctx *ch_ctx,
			struct glink_ch_intent_info *ch_ctx_i);

/**
 * enum ssr_command - G-Link SSR protocol commands
 */
enum ssr_command {
	GLINK_SSR_DO_CLEANUP,
	GLINK_SSR_CLEANUP_DONE,
};

/**
 * struct ssr_notify_data - Contains private data used for client notifications
 *                          from G-Link.
 * tx_done:		Indicates whether or not the tx_done notification has
 *			been received.
 * event:		The state notification event received.
 * responded:		Indicates whether or not a cleanup_done response was
 *			received.
 * edge:		The G-Link edge name for the channel associated with
 *			this callback data
 * do_cleanup_data:	Structure containing the G-Link SSR do_cleanup message.
 */
struct ssr_notify_data {
	bool tx_done;
	unsigned event;
	bool responded;
	const char *edge;
	struct do_cleanup_msg *do_cleanup_data;
};

/**
 * struct subsys_info - Subsystem info structure
 * ssr_name:		name of the subsystem recognized by the SSR framework
 * edge:		name of the G-Link edge
 * xprt:		name of the G-Link transport
 * handle:		glink_ssr channel used for this subsystem
 * link_state_handle:	link state handle for this edge, used to unregister
 *			from receiving link state callbacks
 * link_info:		Transport info used in link state callback registration
 * cb_data:		Private callback data structure for notification
 *			functions
 * subsystem_list_node:	used to chain this structure in a list of subsystem
 *			info structures
 * notify_list:		list of subsys_info_leaf structures, containing the
 *			subsystems to notify if this subsystem undergoes SSR
 * notify_list_len:	length of notify_list
 * link_up:		Flag indicating whether transport is up or not
 * link_up_lock:	Lock for protecting the link_up flag
 */
struct subsys_info {
	const char *ssr_name;
	const char *edge;
	const char *xprt;
	void *handle;
	void *link_state_handle;
	struct glink_link_info *link_info;
	struct ssr_notify_data *cb_data;
	struct list_head subsystem_list_node;
	struct list_head notify_list;
	int notify_list_len;
	bool link_up;
	spinlock_t link_up_lock;
};

/**
 * struct subsys_info_leaf - Subsystem info leaf structure (a subsystem on the
 *                           notify list of a subsys_info structure)
 * ssr_name:	Name of the subsystem recognized by the SSR framework
 * edge:	Name of the G-Link edge
 * xprt:	Name of the G-Link transport
 * restarted:	Indicates whether a restart has been triggered for this edge
 * cb_data:	Private callback data structure for notification functions
 * notify_list_node:	used to chain this structure in the notify list
 */
struct subsys_info_leaf {
	const char *ssr_name;
	const char *edge;
	const char *xprt;
	bool restarted;
	struct ssr_notify_data *cb_data;
	struct list_head notify_list_node;
};

/**
 * struct do_cleanup_msg - The data structure for an SSR do_cleanup message
 * version:	The G-Link SSR protocol version
 * command:	The G-Link SSR command - do_cleanup
 * seq_num:	Sequence number
 * name_len:	Length of the name of the subsystem being restarted
 * name:	G-Link edge name of the subsystem being restarted
 */
struct do_cleanup_msg {
	uint32_t version;
	uint32_t command;
	uint32_t seq_num;
	uint32_t name_len;
	char name[32];
};

/**
 * struct cleanup_done_msg - The data structure for an SSR cleanup_done message
 * version:	The G-Link SSR protocol version
 * response:	The G-Link SSR response to a do_cleanup command, cleanup_done
 * seq_num:	Sequence number
 */
struct cleanup_done_msg {
	uint32_t version;
	uint32_t response;
	uint32_t seq_num;
};

/**
 * get_info_for_subsystem() - Retrieve information about a subsystem from the
 *                            global subsystem_info_list
 * @subsystem:	The name of the subsystem recognized by the SSR
 *		framework
 *
 * Return: subsys_info structure containing info for the requested subsystem;
 *         NULL if no structure can be found for the requested subsystem
 */
struct subsys_info *get_info_for_subsystem(const char *subsystem);

/**
 * get_info_for_edge() - Retrieve information about a subsystem from the
 *                       global subsystem_info_list
 * @edge:	The name of the edge recognized by G-Link
 *
 * Return: subsys_info structure containing info for the requested subsystem;
 *         NULL if no structure can be found for the requested subsystem
 */
struct subsys_info *get_info_for_edge(const char *edge);

/**
 * glink_ssr_get_seq_num() - Get the current SSR sequence number
 *
 * Return: The current SSR sequence number
 */
uint32_t glink_ssr_get_seq_num(void);

/*
 * glink_ssr() - SSR cleanup function.
 *
 * Return: Standard error code.
 */
int glink_ssr(const char *subsystem);

/**
 * notify for subsystem() - Notify other subsystems that a subsystem is being
 *                          restarted
 * @ss_info:	Subsystem info structure for the subsystem being restarted
 *
 * This function sends notifications to affected subsystems that the subsystem
 * in ss_info is being restarted, and waits for the cleanup done response from
 * all of those subsystems. It also initiates any local cleanup that is
 * necessary.
 *
 * Return: 0 on success, standard error codes otherwise
 */
int notify_for_subsystem(struct subsys_info *ss_info);

/**
 * glink_ssr_wait_cleanup_done() - Get the value of the
 *                                 notifications_successful flag in glink_ssr.
 * @timeout_multiplier: timeout multiplier for waiting on all processors
 *
 *
 * Return: True if cleanup_done received from all processors, false otherwise
 */
bool glink_ssr_wait_cleanup_done(unsigned ssr_timeout_multiplier);

struct channel_lcid {
	struct list_head list_node;
	uint32_t lcid;
};

/**
 * struct rwref_lock - Read/Write Reference Lock
 *
 * kref:	reference count
 * read_count:	number of readers that own the lock
 * write_count:	number of writers (max 1) that own the lock
 * count_zero:	used for internal signaling for non-atomic locks
 *
 * A Read/Write Reference Lock is a combination of a read/write spinlock and a
 * refence count.  The main difference is that no locks are held in the
 * critical section and the lifetime of the object is guaranteed.
 *
 * Read Locking
 * Multiple readers may access the lock at any given time and a read lock will
 * also ensure that the object exists for the life of the lock.
 *
 * rwref_read_get()
 *     use resource in "critical section" (no locks are held)
 * rwref_read_put()
 *
 * Write Locking
 * A single writer may access the lock at any given time and a write lock will
 * also ensure that the object exists for the life of the lock.
 *
 * rwref_write_get()
 *     use resource in "critical section" (no locks are held)
 * rwref_write_put()
 *
 * Reference Lock
 * To ensure the lifetime of the lock (and not affect the read or write lock),
 * a simple reference can be done.  By default, rwref_lock_init() will set the
 * reference count to 1.
 *
 * rwref_lock_init()  Reference count is 1
 * rwref_get()        Reference count is 2
 * rwref_put()        Reference count is 1
 * rwref_put()        Reference count goes to 0 and object is destroyed
 */
struct rwref_lock {
	struct kref kref;
	unsigned read_count;
	unsigned write_count;
	spinlock_t lock;
	struct completion count_zero;

	void (*release)(struct rwref_lock *);
};

/**
 * rwref_lock_release() - Initialize rwref_lock
 * lock_ptr:	pointer to lock structure
 */
static inline void rwref_lock_release(struct kref *kref_ptr)
{
	struct rwref_lock *lock_ptr;

	BUG_ON(kref_ptr == NULL);

	lock_ptr = container_of(kref_ptr, struct rwref_lock, kref);
	if (lock_ptr->release)
		lock_ptr->release(lock_ptr);
}

/**
 * rwref_lock_init() - Initialize rwref_lock
 * lock_ptr:	pointer to lock structure
 * release:	release function called when reference count goes to 0
 */
static inline void rwref_lock_init(struct rwref_lock *lock_ptr,
		void (*release)(struct rwref_lock *))
{
	BUG_ON(lock_ptr == NULL);

	kref_init(&lock_ptr->kref);
	lock_ptr->read_count = 0;
	lock_ptr->write_count = 0;
	spin_lock_init(&lock_ptr->lock);
	init_completion(&lock_ptr->count_zero);
	lock_ptr->release = release;
}

/**
 * rwref_get() - gains a reference count for the object
 * lock_ptr:	pointer to lock structure
 */
static inline void rwref_get(struct rwref_lock *lock_ptr)
{
	BUG_ON(lock_ptr == NULL);

	kref_get(&lock_ptr->kref);
}

/**
 * rwref_put() - puts a reference count for the object
 * lock_ptr:	pointer to lock structure
 *
 * If the reference count goes to zero, the release function is called.
 */
static inline void rwref_put(struct rwref_lock *lock_ptr)
{
	BUG_ON(lock_ptr == NULL);

	kref_put(&lock_ptr->kref, rwref_lock_release);
}

/**
 * rwref_read_get() - gains a reference count for a read operation
 * lock_ptr:	pointer to lock structure
 *
 * Multiple readers may acquire the lock as long as the write count is zero.
 */
static inline void rwref_read_get(struct rwref_lock *lock_ptr)
{
	unsigned long flags;

	BUG_ON(lock_ptr == NULL);

	kref_get(&lock_ptr->kref);
	while (1) {
		spin_lock_irqsave(&lock_ptr->lock, flags);
		if (lock_ptr->write_count == 0) {
			lock_ptr->read_count++;
			spin_unlock_irqrestore(&lock_ptr->lock, flags);
			break;
		}
		spin_unlock_irqrestore(&lock_ptr->lock, flags);
		wait_for_completion(&lock_ptr->count_zero);
	}
}

/**
 * rwref_read_put() - returns a reference count for a read operation
 * lock_ptr:	pointer to lock structure
 *
 * Must be preceded by a call to rwref_read_get().
 */
static inline void rwref_read_put(struct rwref_lock *lock_ptr)
{
	unsigned long flags;

	BUG_ON(lock_ptr == NULL);

	spin_lock_irqsave(&lock_ptr->lock, flags);
	BUG_ON(lock_ptr->read_count == 0);
	if (--lock_ptr->read_count == 0)
		complete(&lock_ptr->count_zero);
	spin_unlock_irqrestore(&lock_ptr->lock, flags);
	kref_put(&lock_ptr->kref, rwref_lock_release);
}

/**
 * rwref_write_get() - gains a reference count for a write operation
 * lock_ptr:	pointer to lock structure
 *
 * Only one writer may acquire the lock as long as the reader count is zero.
 */
static inline void rwref_write_get(struct rwref_lock *lock_ptr)
{
	unsigned long flags;

	BUG_ON(lock_ptr == NULL);

	kref_get(&lock_ptr->kref);
	while (1) {
		spin_lock_irqsave(&lock_ptr->lock, flags);
		if (lock_ptr->read_count == 0 && lock_ptr->write_count == 0) {
			lock_ptr->write_count++;
			spin_unlock_irqrestore(&lock_ptr->lock, flags);
			break;
		}
		spin_unlock_irqrestore(&lock_ptr->lock, flags);
		wait_for_completion(&lock_ptr->count_zero);
	}
}

/**
 * rwref_write_put() - returns a reference count for a write operation
 * lock_ptr:	pointer to lock structure
 *
 * Must be preceded by a call to rwref_write_get().
 */
static inline void rwref_write_put(struct rwref_lock *lock_ptr)
{
	unsigned long flags;

	BUG_ON(lock_ptr == NULL);

	spin_lock_irqsave(&lock_ptr->lock, flags);
	BUG_ON(lock_ptr->write_count != 1);
	if (--lock_ptr->write_count == 0)
		complete(&lock_ptr->count_zero);
	spin_unlock_irqrestore(&lock_ptr->lock, flags);
	kref_put(&lock_ptr->kref, rwref_lock_release);
}

#endif /* _SOC_QCOM_GLINK_PRIVATE_H_ */

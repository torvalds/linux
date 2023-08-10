/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ufsqcom

#if !defined(_TRACE_UFS_QCOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_UFS_QCOM_H

#include <linux/tracepoint.h>

#define str_opcode(opcode)						\
	__print_symbolic(opcode,					\
		{ WRITE_16,		"WRITE_16" },			\
		{ WRITE_10,		"WRITE_10" },			\
		{ READ_16,		"READ_16" },			\
		{ READ_10,		"READ_10" },			\
		{ SYNCHRONIZE_CACHE,	"SYNC" },		\
		{ UNMAP,		"UNMAP" })

#define UFS_NOTIFY_CHANGE_STATUS				\
	EM(PRE_CHANGE,	"PRE_CHANGE")		\
	EMe(POST_CHANGE,	"POST_CHANGE")

#define UFS_PM_OP						\
	EM(UFS_RUNTIME_PM,	"UFS_RUNTIME_PM")		\
	EM(UFS_SYSTEM_PM,	"UFS_SYSTEM_PM")		\
	EMe(UFS_SHUTDOWN_PM,	"UFS_SHUTDOWN_PM")

#define UFS_LINK_STATES							\
	EM(UIC_LINK_OFF_STATE,		"UIC_LINK_OFF_STATE")		\
	EM(UIC_LINK_ACTIVE_STATE,	"UIC_LINK_ACTIVE_STATE")	\
	EM(UIC_LINK_HIBERN8_STATE,	"UIC_LINK_HIBERN8_STATE")	\
	EMe(UIC_LINK_BROKEN_STATE,	"UIC_LINK_BROKEN_STATE")

#define UFS_PWR_MODES							\
	EM(UFS_ACTIVE_PWR_MODE,		"UFS_ACTIVE_PWR_MODE")		\
	EM(UFS_SLEEP_PWR_MODE,		"UFS_SLEEP_PWR_MODE")		\
	EM(UFS_POWERDOWN_PWR_MODE,	"UFS_POWERDOWN_PWR_MODE")	\
	EMe(UFS_DEEPSLEEP_PWR_MODE,	"UFS_DEEPSLEEP_PWR_MODE")

#define UFS_CMD_TRACE_STRINGS	\
	EM(UFS_CMD_SEND,    "send_req")         \
	EM(UFS_CMD_COMP,    "complete_rsp")         \
	EM(UFS_DEV_COMP,    "dev_complete")         \
	EM(UFS_QUERY_SEND,  "query_send")           \
	EM(UFS_QUERY_COMP,  "query_complete")       \
	EM(UFS_QUERY_ERR,   "query_complete_err")       \
	EM(UFS_TM_SEND,     "tm_send")          \
	EM(UFS_TM_COMP,     "tm_complete")          \
	EMe(UFS_TM_ERR,     "tm_complete_err")

/* Enums require being exported to userspace, for user tool parsing */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

UFS_NOTIFY_CHANGE_STATUS;
UFS_PM_OP;
UFS_LINK_STATES;
UFS_PWR_MODES;
UFS_CMD_TRACE_STRINGS;

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

TRACE_EVENT(ufs_qcom_pwr_change_notify,

	TP_PROTO(const char *dev_name, int status, u32 gear_rx, u32 pwr_rx,
		u32 hs_rate, int err),

	TP_ARGS(dev_name, status, gear_rx, pwr_rx, hs_rate, err),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__field(int, status)
		__field(u32, gear_rx)
		__field(u32, pwr_rx)
		__field(u32, hs_rate)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->status = status;
		__entry->gear_rx = gear_rx;
		__entry->pwr_rx = pwr_rx;
		__entry->hs_rate = hs_rate;
		__entry->err = err;
	),

	TP_printk("%s: status = %s, gear_rx = %d, pwr_rx = %d, hs_rate = %d, err = %d",
		__get_str(dev_name),
		__print_symbolic(__entry->status, UFS_NOTIFY_CHANGE_STATUS),
		__entry->gear_rx,
		__entry->pwr_rx,
		__entry->hs_rate,
		__entry->err)
);

TRACE_EVENT(ufs_qcom_command,
	TP_PROTO(const char *dev_name, int cmd_t, u8 opcode,
		 unsigned int tag, u32 doorbell, int size),

	TP_ARGS(dev_name, cmd_t, opcode, tag, doorbell, size),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__field(int, cmd_t)
		__field(u8, opcode)
		__field(int, tag)
		__field(u32, doorbell)
		__field(int, size)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->cmd_t = cmd_t;
		__entry->opcode = opcode;
		__entry->tag = tag;
		__entry->doorbell = doorbell;
		__entry->size = size;
	),

	TP_printk(
		"%s: %s: tag: %d, DB: 0x%x, size: %d, opcode: 0x%x (%s)",
		__get_str(dev_name),
		__print_symbolic(__entry->cmd_t, UFS_CMD_TRACE_STRINGS),
		__entry->tag,
		__entry->doorbell,
		__entry->size,
		(u32)__entry->opcode,
		str_opcode(__entry->opcode)
	)
);

TRACE_EVENT(ufs_qcom_uic,
	TP_PROTO(const char *dev_name, int cmd_t, u32 cmd,
		 u32 arg1, u32 arg2, u32 arg3),

	TP_ARGS(dev_name, cmd_t, cmd, arg1, arg2, arg3),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__field(int, cmd_t)
		__field(u32, cmd)
		__field(u32, arg1)
		__field(u32, arg2)
		__field(u32, arg3)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->cmd_t = cmd_t;
		__entry->cmd = cmd;
		__entry->arg1 = arg1;
		__entry->arg2 = arg2;
		__entry->arg3 = arg3;
	),

	TP_printk(
		"%s: %s: cmd: 0x%x, arg1: 0x%x, arg2: 0x%x, arg3: 0x%x",
		 __get_str(dev_name),
		 __print_symbolic(__entry->cmd_t, UFS_CMD_TRACE_STRINGS),
		__entry->cmd,
		__entry->arg1,
		__entry->arg2,
		__entry->arg3
	)
);

TRACE_EVENT(ufs_qcom_hook_check_int_errors,
	TP_PROTO(const char *dev_name, u32 err, u32 uic_err),

	TP_ARGS(dev_name, err, uic_err),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__field(u32, err)
		__field(u32, uic_err)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->err = err;
		__entry->uic_err = uic_err;
	),

	TP_printk(
		"%s: err: 0x%x, uic_err: 0x%x",
		 __get_str(dev_name),
		__entry->err,
		__entry->uic_err
	)
);

TRACE_EVENT(ufs_qcom_shutdown,
	TP_PROTO(const char *dev_name),

	TP_ARGS(dev_name),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
	),

	TP_printk(
		"%s: Going to Shutdown!",
		 __get_str(dev_name)
	)
);

DECLARE_EVENT_CLASS(ufs_qcom_clk_template,
	TP_PROTO(const char *dev_name, int status, bool on, int err),

	TP_ARGS(dev_name, status, on, err),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__field(int, status)
		__field(bool, on)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->status = status;
		__entry->on = on;
		__entry->err = err;
	),

	TP_printk("%s: status = %s, on= %d, err = %d",
		__get_str(dev_name),
		__print_symbolic(__entry->status, UFS_NOTIFY_CHANGE_STATUS),
		__entry->on,
		__entry->err)
);

DEFINE_EVENT(ufs_qcom_clk_template, ufs_qcom_setup_clocks,
		TP_PROTO(const char *dev_name, int status, bool on, int err),
		TP_ARGS(dev_name, status, on, err));
DEFINE_EVENT(ufs_qcom_clk_template, ufs_qcom_clk_scale_notify,
		TP_PROTO(const char *dev_name, int status, bool on, int err),
		TP_ARGS(dev_name, status, on, err));

DECLARE_EVENT_CLASS(ufs_qcom_noify_template,
	TP_PROTO(const char *dev_name, int status, int err),

	TP_ARGS(dev_name, status, err),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__field(int, status)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->status = status;
		__entry->err = err;
	),

	TP_printk("%s: status = %s, err = %d",
		__get_str(dev_name),
		__print_symbolic(__entry->status, UFS_NOTIFY_CHANGE_STATUS),
		__entry->err)
);

DEFINE_EVENT(ufs_qcom_noify_template, ufs_qcom_hce_enable_notify,
		TP_PROTO(const char *dev_name, int status, int err),
		TP_ARGS(dev_name, status, err));

DEFINE_EVENT(ufs_qcom_noify_template, ufs_qcom_link_startup_notify,
		TP_PROTO(const char *dev_name, int status, int err),
		TP_ARGS(dev_name, status, err));


DECLARE_EVENT_CLASS(ufs_qcom_pm_template,
	TP_PROTO(const char *dev_name, int pm_op, int rpm_lvl, int spm_lvl,
		int uic_link_state, int curr_dev_pwr_mode, int err),

	TP_ARGS(dev_name, pm_op, rpm_lvl, spm_lvl, uic_link_state,
		curr_dev_pwr_mode, err),

	TP_STRUCT__entry(
		__string(dev_name, dev_name)
		__field(int, pm_op)
		__field(int, rpm_lvl)
		__field(int, spm_lvl)
		__field(int, uic_link_state)
		__field(int, curr_dev_pwr_mode)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev_name);
		__entry->pm_op = pm_op;
		__entry->rpm_lvl = rpm_lvl;
		__entry->spm_lvl = spm_lvl;
		__entry->uic_link_state = uic_link_state;
		__entry->curr_dev_pwr_mode = curr_dev_pwr_mode;
		__entry->err = err;
	),

	TP_printk(
		"%s: pm_op = %s, rpm_lvl = %d, spm_lvl = %d, link_state = %s, dev_pwr_mode = %d, err = %d",
		__get_str(dev_name),
		__print_symbolic(__entry->pm_op, UFS_PM_OP),
		__entry->rpm_lvl,
		__entry->spm_lvl,
		__print_symbolic(__entry->uic_link_state, UFS_LINK_STATES),
		__print_symbolic(__entry->curr_dev_pwr_mode, UFS_PWR_MODES),
		__entry->err
	)
);

DEFINE_EVENT(ufs_qcom_pm_template, ufs_qcom_suspend,
		TP_PROTO(const char *dev_name, int pm_op, int rpm_lvl, int spm_lvl,
			int uic_link_state, int curr_dev_pwr_mode, int err),
		TP_ARGS(dev_name, pm_op, rpm_lvl, spm_lvl, uic_link_state,
			curr_dev_pwr_mode, err));

DEFINE_EVENT(ufs_qcom_pm_template, ufs_qcom_resume,
		TP_PROTO(const char *dev_name, int pm_op, int rpm_lvl, int spm_lvl,
			int uic_link_state, int curr_dev_pwr_mode, int err),
		TP_ARGS(dev_name, pm_op, rpm_lvl, spm_lvl, uic_link_state,
			curr_dev_pwr_mode, err));


#endif /* if !defined(_TRACE_UFS_QCOM_H) || defined(TRACE_HEADER_MULTI_READ) */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/ufs/host
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ufs-qcom-trace

/* This part must be outside protection */
#include <trace/define_trace.h>

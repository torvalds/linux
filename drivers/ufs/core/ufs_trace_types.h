/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _UFS_TRACE_TYPES_H_
#define _UFS_TRACE_TYPES_H_

enum ufs_trace_str_t {
	UFS_CMD_SEND,
	UFS_CMD_COMP,
	UFS_DEV_COMP,
	UFS_QUERY_SEND,
	UFS_QUERY_COMP,
	UFS_QUERY_ERR,
	UFS_TM_SEND,
	UFS_TM_COMP,
	UFS_TM_ERR
};

enum ufs_trace_tsf_t {
	UFS_TSF_CDB,
	UFS_TSF_OSF,
	UFS_TSF_TM_INPUT,
	UFS_TSF_TM_OUTPUT
};

#endif /* _UFS_TRACE_TYPES_H_ */

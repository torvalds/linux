/* Driver for Realtek RTS51xx USB card reader
 * Header file
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTS51X_TRACE_H
#define __RTS51X_TRACE_H

#include "debug.h"

#define _MSG_TRACE

#ifdef _MSG_TRACE
static inline char *filename(char *path)
{
	char *ptr;

	if (path == NULL)
		return NULL;

	ptr = path;

	while (*ptr != '\0') {
		if ((*ptr == '\\') || (*ptr == '/'))
			path = ptr + 1;
		ptr++;
	}

	return path;
}

#define TRACE_RET(chip, ret)						\
do {									\
	char *_file = filename((char *)__FILE__);			\
	RTS51X_DEBUGP("[%s][%s]:[%d]\n", _file, __func__, __LINE__);	\
	(chip)->trace_msg[(chip)->msg_idx].line = (u16)(__LINE__);	\
	strncpy((chip)->trace_msg[(chip)->msg_idx].func,		\
			__func__, MSG_FUNC_LEN-1);			\
	strncpy((chip)->trace_msg[(chip)->msg_idx].file,		\
			_file, MSG_FILE_LEN-1);				\
	get_current_time((chip)->trace_msg[(chip)->msg_idx].timeval_buf,\
			TIME_VAL_LEN);	\
	(chip)->trace_msg[(chip)->msg_idx].valid = 1;			\
	(chip)->msg_idx++;						\
	if ((chip)->msg_idx >= TRACE_ITEM_CNT) {			\
		(chip)->msg_idx = 0;					\
	}								\
	return ret;							\
} while (0)

#define TRACE_GOTO(chip, label)						\
do {									\
	char *_file = filename((char *)__FILE__);			\
	RTS51X_DEBUGP("[%s][%s]:[%d]\n", _file, __func__, __LINE__);	\
	(chip)->trace_msg[(chip)->msg_idx].line = (u16)(__LINE__);	\
	strncpy((chip)->trace_msg[(chip)->msg_idx].func,		\
			__func__, MSG_FUNC_LEN-1);			\
	strncpy((chip)->trace_msg[(chip)->msg_idx].file,		\
			_file, MSG_FILE_LEN-1);				\
	get_current_time((chip)->trace_msg[(chip)->msg_idx].timeval_buf,\
			TIME_VAL_LEN);					\
	(chip)->trace_msg[(chip)->msg_idx].valid = 1;			\
	(chip)->msg_idx++;						\
	if ((chip)->msg_idx >= TRACE_ITEM_CNT) {			\
		(chip)->msg_idx = 0;					\
	}								\
	goto label;							\
} while (0)
#else
#define TRACE_RET(chip, ret)	return (ret)
#define TRACE_GOTO(chip, label)	goto label
#endif

#ifdef CONFIG_RTS5139_DEBUG
#define RTS51X_DUMP(buf, buf_len)					\
	print_hex_dump(KERN_DEBUG, RTS51X_TIP, DUMP_PREFIX_NONE,	\
				16, 1, (buf), (buf_len), false)

#define CATCH_TRIGGER(chip)					\
do {								\
	rts51x_ep0_write_register((chip), 0xFC31, 0x01, 0x01);	\
	RTS51X_DEBUGP("Catch trigger!\n");			\
} while (0)

#else
#define RTS51X_DUMP(buf, buf_len)
#define CATCH_TRIGGER(chip)
#endif

#endif /* __RTS51X_TRACE_H */

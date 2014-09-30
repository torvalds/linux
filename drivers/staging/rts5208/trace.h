/* Driver for Realtek PCI-Express card reader
 * Header file
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
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
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#ifndef __REALTEK_RTSX_TRACE_H
#define __REALTEK_RTSX_TRACE_H

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
	do {								\
		char *_file = filename(__FILE__);			\
		dev_dbg(rtsx_dev(chip), "[%s][%s]:[%d]\n", _file,	\
			__func__, __LINE__);				\
		(chip)->trace_msg[(chip)->msg_idx].line = (u16)(__LINE__); \
		strncpy((chip)->trace_msg[(chip)->msg_idx].func, __func__, MSG_FUNC_LEN-1); \
		strncpy((chip)->trace_msg[(chip)->msg_idx].file, _file, MSG_FILE_LEN-1); \
		get_current_time((chip)->trace_msg[(chip)->msg_idx].timeval_buf, TIME_VAL_LEN);	\
		(chip)->trace_msg[(chip)->msg_idx].valid = 1;		\
		(chip)->msg_idx++;					\
		if ((chip)->msg_idx >= TRACE_ITEM_CNT) {		\
			(chip)->msg_idx = 0;				\
		}							\
		return ret;						\
	} while (0)

#define TRACE_GOTO(chip, label)						\
	do {								\
		char *_file = filename(__FILE__);			\
		dev_dbg(rtsx_dev(chip), "[%s][%s]:[%d]\n", _file,	\
			__func__, __LINE__);				\
		(chip)->trace_msg[(chip)->msg_idx].line = (u16)(__LINE__); \
		strncpy((chip)->trace_msg[(chip)->msg_idx].func, __func__, MSG_FUNC_LEN-1); \
		strncpy((chip)->trace_msg[(chip)->msg_idx].file, _file, MSG_FILE_LEN-1); \
		get_current_time((chip)->trace_msg[(chip)->msg_idx].timeval_buf, TIME_VAL_LEN);	\
		(chip)->trace_msg[(chip)->msg_idx].valid = 1;		\
		(chip)->msg_idx++;					\
		if ((chip)->msg_idx >= TRACE_ITEM_CNT) {		\
			(chip)->msg_idx = 0;				\
		}							\
		goto label;						\
	} while (0)
#else
#define TRACE_RET(chip, ret)	return ret
#define TRACE_GOTO(chip, label)	goto label
#endif

#ifdef CONFIG_RTS5208_DEBUG
#define RTSX_DUMP(buf, buf_len)					\
	print_hex_dump(KERN_DEBUG, KBUILD_MODNAME ": ",		\
		       DUMP_PREFIX_NONE, 16, 1, (buf), (buf_len), false)
#else
#define RTSX_DUMP(buf, buf_len)
#endif

#endif  /* __REALTEK_RTSX_TRACE_H */

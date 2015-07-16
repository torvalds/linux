#include <linux/kernel.h>
#include <linux/string.h>

#include "rtsx.h"

#ifdef _MSG_TRACE

void _rtsx_trace(struct rtsx_chip *chip, const char *file, const char *func,
		 int line)
{
	struct trace_msg_t *msg = &chip->trace_msg[chip->msg_idx];

	file = kbasename(file);
	dev_dbg(rtsx_dev(chip), "[%s][%s]:[%d]\n", file, func, line);

	strncpy(msg->file, file, MSG_FILE_LEN - 1);
	strncpy(msg->func, func, MSG_FUNC_LEN - 1);
	msg->line = (u16)line;
	get_current_time(msg->timeval_buf, TIME_VAL_LEN);
	msg->valid = 1;

	chip->msg_idx++;
	if (chip->msg_idx >= TRACE_ITEM_CNT)
		chip->msg_idx = 0;
}
#endif

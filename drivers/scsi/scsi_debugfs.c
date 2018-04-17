// SPDX-License-Identifier: GPL-2.0
#include <linux/seq_file.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include "scsi_debugfs.h"

void scsi_show_rq(struct seq_file *m, struct request *rq)
{
	struct scsi_cmnd *cmd = container_of(scsi_req(rq), typeof(*cmd), req);
	int msecs = jiffies_to_msecs(jiffies - cmd->jiffies_at_alloc);
	const u8 *const cdb = READ_ONCE(cmd->cmnd);
	char buf[80] = "(?)";

	if (cdb)
		__scsi_format_command(buf, sizeof(buf), cdb, cmd->cmd_len);
	seq_printf(m, ", .cmd=%s, .retries=%d, allocated %d.%03d s ago", buf,
		   cmd->retries, msecs / 1000, msecs % 1000);
}

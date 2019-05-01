/* SPDX-License-Identifier: GPL-2.0 */
struct request;
struct seq_file;

void scsi_show_rq(struct seq_file *m, struct request *rq);

/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __SNIC_TRC_H
#define __SNIC_TRC_H

#ifdef CONFIG_SCSI_SNIC_DEBUG_FS

extern ssize_t simple_read_from_buffer(void __user *to,
					size_t count,
					loff_t *ppos,
					const void *from,
					size_t available);

extern unsigned int snic_trace_max_pages;

/* Global Data structure for trace to manage trace functionality */
struct snic_trc_data {
	u64	ts;		/* Time Stamp */
	char	*fn;		/* Ptr to Function Name */
	u32	hno;		/* SCSI Host ID */
	u32	tag;		/* Command Tag */
	u64 data[5];
} __attribute__((__packed__));

#define SNIC_TRC_ENTRY_SZ  64	/* in Bytes */

struct snic_trc {
	spinlock_t lock;
	struct snic_trc_data *buf;	/* Trace Buffer */
	u32	max_idx;		/* Max Index into trace buffer */
	u32	rd_idx;
	u32	wr_idx;
	bool	enable;			/* Control Variable for Tracing */

	struct dentry *trc_enable;	/* debugfs file object */
	struct dentry *trc_file;
};

int snic_trc_init(void);
void snic_trc_free(void);
int snic_trc_debugfs_init(void);
void snic_trc_debugfs_term(void);
struct snic_trc_data *snic_get_trc_buf(void);
int snic_get_trc_data(char *buf, int buf_sz);

int snic_debugfs_init(void);
void snic_debugfs_term(void);

static inline void
snic_trace(char *fn, u16 hno, u32 tag, u64 d1, u64 d2, u64 d3, u64 d4, u64 d5)
{
	struct snic_trc_data *tr_rec = snic_get_trc_buf();

	if (!tr_rec)
		return;

	tr_rec->fn = (char *)fn;
	tr_rec->hno = hno;
	tr_rec->tag = tag;
	tr_rec->data[0] = d1;
	tr_rec->data[1] = d2;
	tr_rec->data[2] = d3;
	tr_rec->data[3] = d4;
	tr_rec->data[4] = d5;
	tr_rec->ts = jiffies; /* Update time stamp at last */
}

#define SNIC_TRC(_hno, _tag, d1, d2, d3, d4, d5)			\
	do {								\
		if (unlikely(snic_glob->trc.enable))			\
			snic_trace((char *)__func__,			\
				   (u16)(_hno),				\
				   (u32)(_tag),				\
				   (u64)(d1),				\
				   (u64)(d2),				\
				   (u64)(d3),				\
				   (u64)(d4),				\
				   (u64)(d5));				\
	} while (0)
#else

#define SNIC_TRC(_hno, _tag, d1, d2, d3, d4, d5)	\
	do {						\
		if (unlikely(snic_log_level & 0x2))	\
			SNIC_DBG("SnicTrace: %s %2u %2u %llx %llx %llx %llx %llx", \
				 (char *)__func__,	\
				 (u16)(_hno),		\
				 (u32)(_tag),		\
				 (u64)(d1),		\
				 (u64)(d2),		\
				 (u64)(d3),		\
				 (u64)(d4),		\
				 (u64)(d5));		\
	} while (0)
#endif /* end of CONFIG_SCSI_SNIC_DEBUG_FS */

#define SNIC_TRC_CMD(sc)	\
	((u64)sc->cmnd[0] << 56 | (u64)sc->cmnd[7] << 40 |	\
	 (u64)sc->cmnd[8] << 32 | (u64)sc->cmnd[2] << 24 |	\
	 (u64)sc->cmnd[3] << 16 | (u64)sc->cmnd[4] << 8 |	\
	 (u64)sc->cmnd[5])

#define SNIC_TRC_CMD_STATE_FLAGS(sc)	\
	((u64) CMD_FLAGS(sc) << 32 | CMD_STATE(sc))

#endif /* end of __SNIC_TRC_H */

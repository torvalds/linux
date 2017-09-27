/*
 * Copyright 2012 Cisco Systems, Inc.  All rights reserved.
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

#ifndef __FNIC_TRACE_H__
#define __FNIC_TRACE_H__

#define FNIC_ENTRY_SIZE_BYTES 64
#define FC_TRC_SIZE_BYTES 256
#define FC_TRC_HEADER_SIZE sizeof(struct fc_trace_hdr)

/*
 * Fisrt bit of FNIC_FC_RECV and FNIC_FC_SEND is used to represent the type
 * of frame 1 => Eth frame, 0=> FC frame
 */

#define FNIC_FC_RECV 0x52 /* Character R */
#define FNIC_FC_SEND 0x54 /* Character T */
#define FNIC_FC_LE 0x4C /* Character L */

extern ssize_t simple_read_from_buffer(void __user *to,
					  size_t count,
					  loff_t *ppos,
					  const void *from,
					  size_t available);

extern unsigned int fnic_trace_max_pages;
extern int fnic_tracing_enabled;
extern unsigned int trace_max_pages;

extern unsigned int fnic_fc_trace_max_pages;
extern int fnic_fc_tracing_enabled;
extern int fnic_fc_trace_cleared;

typedef struct fnic_trace_dbg {
	int wr_idx;
	int rd_idx;
	unsigned long *page_offset;
} fnic_trace_dbg_t;

typedef struct fnic_dbgfs {
	int buffer_len;
	char *buffer;
} fnic_dbgfs_t;

struct fnic_trace_data {
	union {
		struct {
			u32 low;
			u32 high;
		};
		u64 val;
	} timestamp, fnaddr;
	u32 host_no;
	u32 tag;
	u64 data[5];
} __attribute__((__packed__));

typedef struct fnic_trace_data fnic_trace_data_t;

struct fc_trace_hdr {
	struct timespec64 time_stamp;
	u32 host_no;
	u8 frame_type;
	u8 frame_len;
} __attribute__((__packed__));

#define FC_TRACE_ADDRESS(a) \
	((unsigned long)(a) + sizeof(struct fc_trace_hdr))

#define FNIC_TRACE_ENTRY_SIZE \
		  (FNIC_ENTRY_SIZE_BYTES - sizeof(fnic_trace_data_t))

#define FNIC_TRACE(_fn, _hn, _t, _a, _b, _c, _d, _e)           \
	if (unlikely(fnic_tracing_enabled)) {                   \
		fnic_trace_data_t *trace_buf = fnic_trace_get_buf(); \
		if (trace_buf) { \
			if (sizeof(unsigned long) < 8) { \
				trace_buf->timestamp.low = jiffies; \
				trace_buf->fnaddr.low = (u32)(unsigned long)_fn; \
			} else { \
				trace_buf->timestamp.val = jiffies; \
				trace_buf->fnaddr.val = (u64)(unsigned long)_fn; \
			} \
			trace_buf->host_no = _hn; \
			trace_buf->tag = _t; \
			trace_buf->data[0] = (u64)(unsigned long)_a; \
			trace_buf->data[1] = (u64)(unsigned long)_b; \
			trace_buf->data[2] = (u64)(unsigned long)_c; \
			trace_buf->data[3] = (u64)(unsigned long)_d; \
			trace_buf->data[4] = (u64)(unsigned long)_e; \
		} \
	}

fnic_trace_data_t *fnic_trace_get_buf(void);
int fnic_get_trace_data(fnic_dbgfs_t *);
int fnic_trace_buf_init(void);
void fnic_trace_free(void);
int fnic_debugfs_init(void);
void fnic_debugfs_terminate(void);
int fnic_trace_debugfs_init(void);
void fnic_trace_debugfs_terminate(void);

/* Fnic FC CTLR Trace releated function */
int fnic_fc_trace_init(void);
void fnic_fc_trace_free(void);
int fnic_fc_trace_set_data(u32 host_no, u8 frame_type,
				char *frame, u32 fc_frame_len);
int fnic_fc_trace_get_data(fnic_dbgfs_t *fnic_dbgfs_prt, u8 rdata_flag);
void copy_and_format_trace_data(struct fc_trace_hdr *tdata,
				fnic_dbgfs_t *fnic_dbgfs_prt,
				int *len, u8 rdata_flag);
int fnic_fc_trace_debugfs_init(void);
void fnic_fc_trace_debugfs_terminate(void);

#endif

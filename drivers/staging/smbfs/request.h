#include <linux/list.h>
#include <linux/types.h>
#include <linux/uio.h>
#include <linux/wait.h>

struct smb_request {
	struct list_head rq_queue;	/* recvq or xmitq for the server */

	atomic_t rq_count;

	wait_queue_head_t rq_wait;
	int rq_flags;
	int rq_mid;	/* multiplex ID, set by request.c */

	struct smb_sb_info *rq_server;

	/* header + word count + parameter words + byte count */
	unsigned char rq_header[SMB_HEADER_LEN + 20*2 + 2];

	int rq_bufsize;
	unsigned char *rq_buffer;

	/* FIXME: this is not good enough for merging IO requests. */
	unsigned char *rq_page;
	int rq_rsize;

	int rq_resp_wct;
	int rq_resp_bcc;

	int rq_rlen;
	int rq_bytes_recvd;

	int rq_slen;
	int rq_bytes_sent;

	int rq_iovlen;
	struct kvec rq_iov[4];

	int (*rq_setup_read) (struct smb_request *);
	void (*rq_callback) (struct smb_request *);

	/* ------ trans2 stuff ------ */

	u16 rq_trans2_command;	/* 0 if not a trans2 request */
	unsigned int rq_ldata;
	unsigned char *rq_data;
	unsigned int rq_lparm;
	unsigned char *rq_parm;

	int rq_fragment;
	u32 rq_total_data;
	u32 rq_total_parm;
	int rq_trans2bufsize;
	unsigned char *rq_trans2buffer;

	/* ------ response ------ */

	unsigned short rq_rcls;
	unsigned short rq_err;
	int rq_errno;
};

#define SMB_REQ_STATIC		0x0001	/* rq_buffer is static */
#define SMB_REQ_NORETRY		0x0002	/* request is invalid after retry */

#define SMB_REQ_TRANSMITTED	0x4000	/* all data has been sent */
#define SMB_REQ_RECEIVED	0x8000	/* reply received, smbiod is done */

#define xSMB_REQ_NOREPLY	0x0004	/* we don't want the reply (if any) */
#define xSMB_REQ_NORECEIVER	0x0008	/* caller doesn't wait for response */

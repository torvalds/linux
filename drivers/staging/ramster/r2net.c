/*
 * r2net.c
 *
 * Copyright (c) 2011, Dan Magenheimer, Oracle Corp.
 *
 * Ramster_r2net provides an interface between zcache and r2net.
 *
 * FIXME: support more than two nodes
 */

#include <linux/list.h>
#include "cluster/tcp.h"
#include "cluster/nodemanager.h"
#include "tmem.h"
#include "zcache.h"
#include "ramster.h"

#define RAMSTER_TESTING

#define RMSTR_KEY	0x77347734

enum {
	RMSTR_TMEM_PUT_EPH = 100,
	RMSTR_TMEM_PUT_PERS,
	RMSTR_TMEM_ASYNC_GET_REQUEST,
	RMSTR_TMEM_ASYNC_GET_AND_FREE_REQUEST,
	RMSTR_TMEM_ASYNC_GET_REPLY,
	RMSTR_TMEM_FLUSH,
	RMSTR_TMEM_FLOBJ,
	RMSTR_TMEM_DESTROY_POOL,
};

#define RMSTR_R2NET_MAX_LEN \
		(R2NET_MAX_PAYLOAD_BYTES - sizeof(struct tmem_xhandle))

#include "cluster/tcp_internal.h"

static struct r2nm_node *r2net_target_node;
static int r2net_target_nodenum;

int r2net_remote_target_node_set(int node_num)
{
	int ret = -1;

	r2net_target_node = r2nm_get_node_by_num(node_num);
	if (r2net_target_node != NULL) {
		r2net_target_nodenum = node_num;
		r2nm_node_put(r2net_target_node);
		ret = 0;
	}
	return ret;
}

/* FIXME following buffer should be per-cpu, protected by preempt_disable */
static char ramster_async_get_buf[R2NET_MAX_PAYLOAD_BYTES];

static int ramster_remote_async_get_request_handler(struct r2net_msg *msg,
				u32 len, void *data, void **ret_data)
{
	char *pdata;
	struct tmem_xhandle xh;
	int found;
	size_t size = RMSTR_R2NET_MAX_LEN;
	u16 msgtype = be16_to_cpu(msg->msg_type);
	bool get_and_free = (msgtype == RMSTR_TMEM_ASYNC_GET_AND_FREE_REQUEST);
	unsigned long flags;

	xh = *(struct tmem_xhandle *)msg->buf;
	if (xh.xh_data_size > RMSTR_R2NET_MAX_LEN)
		BUG();
	pdata = ramster_async_get_buf;
	*(struct tmem_xhandle *)pdata = xh;
	pdata += sizeof(struct tmem_xhandle);
	local_irq_save(flags);
	found = zcache_get(xh.client_id, xh.pool_id, &xh.oid, xh.index,
				pdata, &size, 1, get_and_free ? 1 : -1);
	local_irq_restore(flags);
	if (found < 0) {
		/* a zero size indicates the get failed */
		size = 0;
	}
	if (size > RMSTR_R2NET_MAX_LEN)
		BUG();
	*ret_data = pdata - sizeof(struct tmem_xhandle);
	/* now make caller (r2net_process_message) handle specially */
	r2net_force_data_magic(msg, RMSTR_TMEM_ASYNC_GET_REPLY, RMSTR_KEY);
	return size + sizeof(struct tmem_xhandle);
}

static int ramster_remote_async_get_reply_handler(struct r2net_msg *msg,
				u32 len, void *data, void **ret_data)
{
	char *in = (char *)msg->buf;
	int datalen = len - sizeof(struct r2net_msg);
	int ret = -1;
	struct tmem_xhandle *xh = (struct tmem_xhandle *)in;

	in += sizeof(struct tmem_xhandle);
	datalen -= sizeof(struct tmem_xhandle);
	BUG_ON(datalen < 0 || datalen > PAGE_SIZE);
	ret = zcache_localify(xh->pool_id, &xh->oid, xh->index,
				in, datalen, xh->extra);
#ifdef RAMSTER_TESTING
	if (ret == -EEXIST)
		pr_err("TESTING ArrgREP, aborted overwrite on racy put\n");
#endif
	return ret;
}

int ramster_remote_put_handler(struct r2net_msg *msg,
				u32 len, void *data, void **ret_data)
{
	struct tmem_xhandle *xh;
	char *p = (char *)msg->buf;
	int datalen = len - sizeof(struct r2net_msg) -
				sizeof(struct tmem_xhandle);
	u16 msgtype = be16_to_cpu(msg->msg_type);
	bool ephemeral = (msgtype == RMSTR_TMEM_PUT_EPH);
	unsigned long flags;
	int ret;

	xh = (struct tmem_xhandle *)p;
	p += sizeof(struct tmem_xhandle);
	zcache_autocreate_pool(xh->client_id, xh->pool_id, ephemeral);
	local_irq_save(flags);
	ret = zcache_put(xh->client_id, xh->pool_id, &xh->oid, xh->index,
				p, datalen, 1, ephemeral ? 1 : -1);
	local_irq_restore(flags);
	return ret;
}

int ramster_remote_flush_handler(struct r2net_msg *msg,
				u32 len, void *data, void **ret_data)
{
	struct tmem_xhandle *xh;
	char *p = (char *)msg->buf;

	xh = (struct tmem_xhandle *)p;
	p += sizeof(struct tmem_xhandle);
	(void)zcache_flush(xh->client_id, xh->pool_id, &xh->oid, xh->index);
	return 0;
}

int ramster_remote_flobj_handler(struct r2net_msg *msg,
				u32 len, void *data, void **ret_data)
{
	struct tmem_xhandle *xh;
	char *p = (char *)msg->buf;

	xh = (struct tmem_xhandle *)p;
	p += sizeof(struct tmem_xhandle);
	(void)zcache_flush_object(xh->client_id, xh->pool_id, &xh->oid);
	return 0;
}

int ramster_remote_async_get(struct tmem_xhandle *xh, bool free, int remotenode,
				size_t expect_size, uint8_t expect_cksum,
				void *extra)
{
	int ret = -1, status;
	struct r2nm_node *node = NULL;
	struct kvec vec[1];
	size_t veclen = 1;
	u32 msg_type;

	node = r2nm_get_node_by_num(remotenode);
	if (node == NULL)
		goto out;
	xh->client_id = r2nm_this_node(); /* which node is getting */
	xh->xh_data_cksum = expect_cksum;
	xh->xh_data_size = expect_size;
	xh->extra = extra;
	vec[0].iov_len = sizeof(*xh);
	vec[0].iov_base = xh;
	if (free)
		msg_type = RMSTR_TMEM_ASYNC_GET_AND_FREE_REQUEST;
	else
		msg_type = RMSTR_TMEM_ASYNC_GET_REQUEST;
	ret = r2net_send_message_vec(msg_type, RMSTR_KEY,
					vec, veclen, remotenode, &status);
	r2nm_node_put(node);
	if (ret < 0) {
		/* FIXME handle bad message possibilities here? */
		pr_err("UNTESTED ret<0 in ramster_remote_async_get\n");
	}
	ret = status;
out:
	return ret;
}

#ifdef RAMSTER_TESTING
/* leave me here to see if it catches a weird crash */
static void ramster_check_irq_counts(void)
{
	static int last_hardirq_cnt, last_softirq_cnt, last_preempt_cnt;
	int cur_hardirq_cnt, cur_softirq_cnt, cur_preempt_cnt;

	cur_hardirq_cnt = hardirq_count() >> HARDIRQ_SHIFT;
	if (cur_hardirq_cnt > last_hardirq_cnt) {
		last_hardirq_cnt = cur_hardirq_cnt;
		if (!(last_hardirq_cnt&(last_hardirq_cnt-1)))
			pr_err("RAMSTER TESTING RRP hardirq_count=%d\n",
				last_hardirq_cnt);
	}
	cur_softirq_cnt = softirq_count() >> SOFTIRQ_SHIFT;
	if (cur_softirq_cnt > last_softirq_cnt) {
		last_softirq_cnt = cur_softirq_cnt;
		if (!(last_softirq_cnt&(last_softirq_cnt-1)))
			pr_err("RAMSTER TESTING RRP softirq_count=%d\n",
				last_softirq_cnt);
	}
	cur_preempt_cnt = preempt_count() & PREEMPT_MASK;
	if (cur_preempt_cnt > last_preempt_cnt) {
		last_preempt_cnt = cur_preempt_cnt;
		if (!(last_preempt_cnt&(last_preempt_cnt-1)))
			pr_err("RAMSTER TESTING RRP preempt_count=%d\n",
				last_preempt_cnt);
	}
}
#endif

int ramster_remote_put(struct tmem_xhandle *xh, char *data, size_t size,
				bool ephemeral, int *remotenode)
{
	int nodenum, ret = -1, status;
	struct r2nm_node *node = NULL;
	struct kvec vec[2];
	size_t veclen = 2;
	u32 msg_type;
#ifdef RAMSTER_TESTING
	struct r2net_node *nn;
#endif

	BUG_ON(size > RMSTR_R2NET_MAX_LEN);
	xh->client_id = r2nm_this_node(); /* which node is putting */
	vec[0].iov_len = sizeof(*xh);
	vec[0].iov_base = xh;
	vec[1].iov_len = size;
	vec[1].iov_base = data;
	node = r2net_target_node;
	if (!node)
		goto out;

	nodenum = r2net_target_nodenum;

	r2nm_node_get(node);

#ifdef RAMSTER_TESTING
	nn = r2net_nn_from_num(nodenum);
	WARN_ON_ONCE(nn->nn_persistent_error || !nn->nn_sc_valid);
#endif

	if (ephemeral)
		msg_type = RMSTR_TMEM_PUT_EPH;
	else
		msg_type = RMSTR_TMEM_PUT_PERS;
#ifdef RAMSTER_TESTING
	/* leave me here to see if it catches a weird crash */
	ramster_check_irq_counts();
#endif

	ret = r2net_send_message_vec(msg_type, RMSTR_KEY, vec, veclen,
						nodenum, &status);
#ifdef RAMSTER_TESTING
	if (ret != 0) {
		static unsigned long cnt;
		cnt++;
		if (!(cnt&(cnt-1)))
			pr_err("ramster_remote_put: message failed, ret=%d, cnt=%lu\n",
				ret, cnt);
		ret = -1;
	}
#endif
	if (ret < 0)
		ret = -1;
	else {
		ret = status;
		*remotenode = nodenum;
	}

	r2nm_node_put(node);
out:
	return ret;
}

int ramster_remote_flush(struct tmem_xhandle *xh, int remotenode)
{
	int ret = -1, status;
	struct r2nm_node *node = NULL;
	struct kvec vec[1];
	size_t veclen = 1;

	node = r2nm_get_node_by_num(remotenode);
	BUG_ON(node == NULL);
	xh->client_id = r2nm_this_node(); /* which node is flushing */
	vec[0].iov_len = sizeof(*xh);
	vec[0].iov_base = xh;
	BUG_ON(irqs_disabled());
	BUG_ON(in_softirq());
	ret = r2net_send_message_vec(RMSTR_TMEM_FLUSH, RMSTR_KEY,
					vec, veclen, remotenode, &status);
	r2nm_node_put(node);
	return ret;
}

int ramster_remote_flush_object(struct tmem_xhandle *xh, int remotenode)
{
	int ret = -1, status;
	struct r2nm_node *node = NULL;
	struct kvec vec[1];
	size_t veclen = 1;

	node = r2nm_get_node_by_num(remotenode);
	BUG_ON(node == NULL);
	xh->client_id = r2nm_this_node(); /* which node is flobjing */
	vec[0].iov_len = sizeof(*xh);
	vec[0].iov_base = xh;
	ret = r2net_send_message_vec(RMSTR_TMEM_FLOBJ, RMSTR_KEY,
					vec, veclen, remotenode, &status);
	r2nm_node_put(node);
	return ret;
}

/*
 * Handler registration
 */

static LIST_HEAD(r2net_unreg_list);

static void r2net_unregister_handlers(void)
{
	r2net_unregister_handler_list(&r2net_unreg_list);
}

int r2net_register_handlers(void)
{
	int status;

	status = r2net_register_handler(RMSTR_TMEM_PUT_EPH, RMSTR_KEY,
				RMSTR_R2NET_MAX_LEN,
				ramster_remote_put_handler,
				NULL, NULL, &r2net_unreg_list);
	if (status)
		goto bail;

	status = r2net_register_handler(RMSTR_TMEM_PUT_PERS, RMSTR_KEY,
				RMSTR_R2NET_MAX_LEN,
				ramster_remote_put_handler,
				NULL, NULL, &r2net_unreg_list);
	if (status)
		goto bail;

	status = r2net_register_handler(RMSTR_TMEM_ASYNC_GET_REQUEST, RMSTR_KEY,
				RMSTR_R2NET_MAX_LEN,
				ramster_remote_async_get_request_handler,
				NULL, NULL,
				&r2net_unreg_list);
	if (status)
		goto bail;

	status = r2net_register_handler(RMSTR_TMEM_ASYNC_GET_AND_FREE_REQUEST,
				RMSTR_KEY, RMSTR_R2NET_MAX_LEN,
				ramster_remote_async_get_request_handler,
				NULL, NULL,
				&r2net_unreg_list);
	if (status)
		goto bail;

	status = r2net_register_handler(RMSTR_TMEM_ASYNC_GET_REPLY, RMSTR_KEY,
				RMSTR_R2NET_MAX_LEN,
				ramster_remote_async_get_reply_handler,
				NULL, NULL,
				&r2net_unreg_list);
	if (status)
		goto bail;

	status = r2net_register_handler(RMSTR_TMEM_FLUSH, RMSTR_KEY,
				RMSTR_R2NET_MAX_LEN,
				ramster_remote_flush_handler,
				NULL, NULL,
				&r2net_unreg_list);
	if (status)
		goto bail;

	status = r2net_register_handler(RMSTR_TMEM_FLOBJ, RMSTR_KEY,
				RMSTR_R2NET_MAX_LEN,
				ramster_remote_flobj_handler,
				NULL, NULL,
				&r2net_unreg_list);
	if (status)
		goto bail;

	pr_info("ramster: r2net handlers registered\n");

bail:
	if (status) {
		r2net_unregister_handlers();
		pr_err("ramster: couldn't register r2net handlers\n");
	}
	return status;
}

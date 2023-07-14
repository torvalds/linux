// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/qrtr.h>
#include <linux/net.h>
#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/string.h>
#include <net/sock.h>
#include <linux/workqueue.h>
#include <trace/events/sock.h>
#include <linux/soc/qcom/qmi.h>

static struct socket *qmi_sock_create(struct qmi_handle *qmi,
				      struct sockaddr_qrtr *sq);

/**
 * qmi_recv_new_server() - handler of NEW_SERVER control message
 * @qmi:	qmi handle
 * @service:	service id of the new server
 * @instance:	instance id of the new server
 * @node:	node of the new server
 * @port:	port of the new server
 *
 * Calls the new_server callback to inform the client about a newly registered
 * server matching the currently registered service lookup.
 */
static void qmi_recv_new_server(struct qmi_handle *qmi,
				unsigned int service, unsigned int instance,
				unsigned int node, unsigned int port)
{
	struct qmi_ops *ops = &qmi->ops;
	struct qmi_service *svc;
	int ret;

	if (!ops->new_server)
		return;

	/* Ignore EOF marker */
	if (!node && !port)
		return;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return;

	svc->service = service;
	svc->version = instance & 0xff;
	svc->instance = instance >> 8;
	svc->node = node;
	svc->port = port;

	ret = ops->new_server(qmi, svc);
	if (ret < 0)
		kfree(svc);
	else
		list_add(&svc->list_node, &qmi->lookup_results);
}

/**
 * qmi_recv_del_server() - handler of DEL_SERVER control message
 * @qmi:	qmi handle
 * @node:	node of the dying server, a value of -1 matches all nodes
 * @port:	port of the dying server, a value of -1 matches all ports
 *
 * Calls the del_server callback for each previously seen server, allowing the
 * client to react to the disappearing server.
 */
static void qmi_recv_del_server(struct qmi_handle *qmi,
				unsigned int node, unsigned int port)
{
	struct qmi_ops *ops = &qmi->ops;
	struct qmi_service *svc;
	struct qmi_service *tmp;

	list_for_each_entry_safe(svc, tmp, &qmi->lookup_results, list_node) {
		if (node != -1 && svc->node != node)
			continue;
		if (port != -1 && svc->port != port)
			continue;

		if (ops->del_server)
			ops->del_server(qmi, svc);

		list_del(&svc->list_node);
		kfree(svc);
	}
}

/**
 * qmi_recv_bye() - handler of BYE control message
 * @qmi:	qmi handle
 * @node:	id of the dying node
 *
 * Signals the client that all previously registered services on this node are
 * now gone and then calls the bye callback to allow the client further
 * cleaning up resources associated with this remote.
 */
static void qmi_recv_bye(struct qmi_handle *qmi,
			 unsigned int node)
{
	struct qmi_ops *ops = &qmi->ops;

	qmi_recv_del_server(qmi, node, -1);

	if (ops->bye)
		ops->bye(qmi, node);
}

/**
 * qmi_recv_del_client() - handler of DEL_CLIENT control message
 * @qmi:	qmi handle
 * @node:	node of the dying client
 * @port:	port of the dying client
 *
 * Signals the client about a dying client, by calling the del_client callback.
 */
static void qmi_recv_del_client(struct qmi_handle *qmi,
				unsigned int node, unsigned int port)
{
	struct qmi_ops *ops = &qmi->ops;

	if (ops->del_client)
		ops->del_client(qmi, node, port);
}

static void qmi_recv_ctrl_pkt(struct qmi_handle *qmi,
			      const void *buf, size_t len)
{
	const struct qrtr_ctrl_pkt *pkt = buf;

	if (len < sizeof(struct qrtr_ctrl_pkt)) {
		pr_debug("ignoring short control packet\n");
		return;
	}

	switch (le32_to_cpu(pkt->cmd)) {
	case QRTR_TYPE_BYE:
		qmi_recv_bye(qmi, le32_to_cpu(pkt->client.node));
		break;
	case QRTR_TYPE_NEW_SERVER:
		qmi_recv_new_server(qmi,
				    le32_to_cpu(pkt->server.service),
				    le32_to_cpu(pkt->server.instance),
				    le32_to_cpu(pkt->server.node),
				    le32_to_cpu(pkt->server.port));
		break;
	case QRTR_TYPE_DEL_SERVER:
		qmi_recv_del_server(qmi,
				    le32_to_cpu(pkt->server.node),
				    le32_to_cpu(pkt->server.port));
		break;
	case QRTR_TYPE_DEL_CLIENT:
		qmi_recv_del_client(qmi,
				    le32_to_cpu(pkt->client.node),
				    le32_to_cpu(pkt->client.port));
		break;
	}
}

static void qmi_send_new_lookup(struct qmi_handle *qmi, struct qmi_service *svc)
{
	struct qrtr_ctrl_pkt pkt;
	struct sockaddr_qrtr sq;
	struct msghdr msg = { };
	struct kvec iv = { &pkt, sizeof(pkt) };
	int ret;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_NEW_LOOKUP);
	pkt.server.service = cpu_to_le32(svc->service);
	pkt.server.instance = cpu_to_le32(svc->version | svc->instance << 8);

	sq.sq_family = qmi->sq.sq_family;
	sq.sq_node = qmi->sq.sq_node;
	sq.sq_port = QRTR_PORT_CTRL;

	msg.msg_name = &sq;
	msg.msg_namelen = sizeof(sq);

	mutex_lock(&qmi->sock_lock);
	if (qmi->sock) {
		ret = kernel_sendmsg(qmi->sock, &msg, &iv, 1, sizeof(pkt));
		if (ret < 0)
			pr_err("failed to send lookup registration: %d\n", ret);
	}
	mutex_unlock(&qmi->sock_lock);
}

/**
 * qmi_add_lookup() - register a new lookup with the name service
 * @qmi:	qmi handle
 * @service:	service id of the request
 * @instance:	instance id of the request
 * @version:	version number of the request
 *
 * Registering a lookup query with the name server will cause the name server
 * to send NEW_SERVER and DEL_SERVER control messages to this socket as
 * matching services are registered.
 *
 * Return: 0 on success, negative errno on failure.
 */
int qmi_add_lookup(struct qmi_handle *qmi, unsigned int service,
		   unsigned int version, unsigned int instance)
{
	struct qmi_service *svc;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->service = service;
	svc->version = version;
	svc->instance = instance;

	list_add(&svc->list_node, &qmi->lookups);

	qmi_send_new_lookup(qmi, svc);

	return 0;
}
EXPORT_SYMBOL(qmi_add_lookup);

static void qmi_send_new_server(struct qmi_handle *qmi, struct qmi_service *svc)
{
	struct qrtr_ctrl_pkt pkt;
	struct sockaddr_qrtr sq;
	struct msghdr msg = { };
	struct kvec iv = { &pkt, sizeof(pkt) };
	int ret;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_NEW_SERVER);
	pkt.server.service = cpu_to_le32(svc->service);
	pkt.server.instance = cpu_to_le32(svc->version | svc->instance << 8);
	pkt.server.node = cpu_to_le32(qmi->sq.sq_node);
	pkt.server.port = cpu_to_le32(qmi->sq.sq_port);

	sq.sq_family = qmi->sq.sq_family;
	sq.sq_node = qmi->sq.sq_node;
	sq.sq_port = QRTR_PORT_CTRL;

	msg.msg_name = &sq;
	msg.msg_namelen = sizeof(sq);

	mutex_lock(&qmi->sock_lock);
	if (qmi->sock) {
		ret = kernel_sendmsg(qmi->sock, &msg, &iv, 1, sizeof(pkt));
		if (ret < 0)
			pr_err("send service registration failed: %d\n", ret);
	}
	mutex_unlock(&qmi->sock_lock);
}

/**
 * qmi_add_server() - register a service with the name service
 * @qmi:	qmi handle
 * @service:	type of the service
 * @instance:	instance of the service
 * @version:	version of the service
 *
 * Register a new service with the name service. This allows clients to find
 * and start sending messages to the client associated with @qmi.
 *
 * Return: 0 on success, negative errno on failure.
 */
int qmi_add_server(struct qmi_handle *qmi, unsigned int service,
		   unsigned int version, unsigned int instance)
{
	struct qmi_service *svc;

	svc = kzalloc(sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->service = service;
	svc->version = version;
	svc->instance = instance;

	list_add(&svc->list_node, &qmi->services);

	qmi_send_new_server(qmi, svc);

	return 0;
}
EXPORT_SYMBOL(qmi_add_server);

/**
 * qmi_txn_init() - allocate transaction id within the given QMI handle
 * @qmi:	QMI handle
 * @txn:	transaction context
 * @ei:		description of how to decode a matching response (optional)
 * @c_struct:	pointer to the object to decode the response into (optional)
 *
 * This allocates a transaction id within the QMI handle. If @ei and @c_struct
 * are specified any responses to this transaction will be decoded as described
 * by @ei into @c_struct.
 *
 * A client calling qmi_txn_init() must call either qmi_txn_wait() or
 * qmi_txn_cancel() to free up the allocated resources.
 *
 * Return: Transaction id on success, negative errno on failure.
 */
int qmi_txn_init(struct qmi_handle *qmi, struct qmi_txn *txn,
		 const struct qmi_elem_info *ei, void *c_struct)
{
	int ret;

	memset(txn, 0, sizeof(*txn));

	mutex_init(&txn->lock);
	init_completion(&txn->completion);
	txn->qmi = qmi;
	txn->ei = ei;
	txn->dest = c_struct;

	mutex_lock(&qmi->txn_lock);
	ret = idr_alloc_cyclic(&qmi->txns, txn, 0, U16_MAX, GFP_KERNEL);
	if (ret < 0)
		pr_err("failed to allocate transaction id\n");

	txn->id = ret;
	mutex_unlock(&qmi->txn_lock);

	return ret;
}
EXPORT_SYMBOL(qmi_txn_init);

/**
 * qmi_txn_wait() - wait for a response on a transaction
 * @txn:	transaction handle
 * @timeout:	timeout, in jiffies
 *
 * If the transaction is decoded by the means of @ei and @c_struct the return
 * value will be the returned value of qmi_decode_message(), otherwise it's up
 * to the specified message handler to fill out the result.
 *
 * Return: the transaction response on success, negative errno on failure.
 */
int qmi_txn_wait(struct qmi_txn *txn, unsigned long timeout)
{
	struct qmi_handle *qmi = txn->qmi;
	int ret;

	ret = wait_for_completion_timeout(&txn->completion, timeout);

	mutex_lock(&qmi->txn_lock);
	mutex_lock(&txn->lock);
	idr_remove(&qmi->txns, txn->id);
	mutex_unlock(&txn->lock);
	mutex_unlock(&qmi->txn_lock);

	if (ret == 0)
		return -ETIMEDOUT;
	else
		return txn->result;
}
EXPORT_SYMBOL(qmi_txn_wait);

/**
 * qmi_txn_cancel() - cancel an ongoing transaction
 * @txn:	transaction id
 */
void qmi_txn_cancel(struct qmi_txn *txn)
{
	struct qmi_handle *qmi = txn->qmi;

	mutex_lock(&qmi->txn_lock);
	mutex_lock(&txn->lock);
	idr_remove(&qmi->txns, txn->id);
	mutex_unlock(&txn->lock);
	mutex_unlock(&qmi->txn_lock);
}
EXPORT_SYMBOL(qmi_txn_cancel);

/**
 * qmi_invoke_handler() - find and invoke a handler for a message
 * @qmi:	qmi handle
 * @sq:		sockaddr of the sender
 * @txn:	transaction object for the message
 * @buf:	buffer containing the message
 * @len:	length of @buf
 *
 * Find handler and invoke handler for the incoming message.
 */
static void qmi_invoke_handler(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			       struct qmi_txn *txn, const void *buf, size_t len)
{
	const struct qmi_msg_handler *handler;
	const struct qmi_header *hdr = buf;
	void *dest;
	int ret;

	if (!qmi->handlers)
		return;

	for (handler = qmi->handlers; handler->fn; handler++) {
		if (handler->type == hdr->type &&
		    handler->msg_id == hdr->msg_id)
			break;
	}

	if (!handler->fn)
		return;

	dest = kzalloc(handler->decoded_size, GFP_KERNEL);
	if (!dest)
		return;

	ret = qmi_decode_message(buf, len, handler->ei, dest);
	if (ret < 0)
		pr_err("failed to decode incoming message\n");
	else
		handler->fn(qmi, sq, txn, dest);

	kfree(dest);
}

/**
 * qmi_handle_net_reset() - invoked to handle ENETRESET on a QMI handle
 * @qmi:	the QMI context
 *
 * As a result of registering a name service with the QRTR all open sockets are
 * flagged with ENETRESET and this function will be called. The typical case is
 * the initial boot, where this signals that the local node id has been
 * configured and as such any bound sockets needs to be rebound. So close the
 * socket, inform the client and re-initialize the socket.
 *
 * For clients it's generally sufficient to react to the del_server callbacks,
 * but server code is expected to treat the net_reset callback as a "bye" from
 * all nodes.
 *
 * Finally the QMI handle will send out registration requests for any lookups
 * and services.
 */
static void qmi_handle_net_reset(struct qmi_handle *qmi)
{
	struct sockaddr_qrtr sq;
	struct qmi_service *svc;
	struct socket *sock;

	sock = qmi_sock_create(qmi, &sq);
	if (IS_ERR(sock))
		return;

	mutex_lock(&qmi->sock_lock);
	sock_release(qmi->sock);
	qmi->sock = NULL;
	mutex_unlock(&qmi->sock_lock);

	qmi_recv_del_server(qmi, -1, -1);

	if (qmi->ops.net_reset)
		qmi->ops.net_reset(qmi);

	mutex_lock(&qmi->sock_lock);
	qmi->sock = sock;
	qmi->sq = sq;
	mutex_unlock(&qmi->sock_lock);

	list_for_each_entry(svc, &qmi->lookups, list_node)
		qmi_send_new_lookup(qmi, svc);

	list_for_each_entry(svc, &qmi->services, list_node)
		qmi_send_new_server(qmi, svc);
}

static void qmi_handle_message(struct qmi_handle *qmi,
			       struct sockaddr_qrtr *sq,
			       const void *buf, size_t len)
{
	const struct qmi_header *hdr;
	struct qmi_txn tmp_txn;
	struct qmi_txn *txn = NULL;
	int ret;

	if (len < sizeof(*hdr)) {
		pr_err("ignoring short QMI packet\n");
		return;
	}

	hdr = buf;

	/* If this is a response, find the matching transaction handle */
	if (hdr->type == QMI_RESPONSE) {
		mutex_lock(&qmi->txn_lock);
		txn = idr_find(&qmi->txns, hdr->txn_id);

		/* Ignore unexpected responses */
		if (!txn) {
			mutex_unlock(&qmi->txn_lock);
			return;
		}

		mutex_lock(&txn->lock);
		mutex_unlock(&qmi->txn_lock);

		if (txn->dest && txn->ei) {
			ret = qmi_decode_message(buf, len, txn->ei, txn->dest);
			if (ret < 0)
				pr_err("failed to decode incoming message\n");

			txn->result = ret;
			complete(&txn->completion);
		} else  {
			qmi_invoke_handler(qmi, sq, txn, buf, len);
		}

		mutex_unlock(&txn->lock);
	} else {
		/* Create a txn based on the txn_id of the incoming message */
		memset(&tmp_txn, 0, sizeof(tmp_txn));
		tmp_txn.id = hdr->txn_id;

		qmi_invoke_handler(qmi, sq, &tmp_txn, buf, len);
	}
}

static void qmi_data_ready_work(struct work_struct *work)
{
	struct qmi_handle *qmi = container_of(work, struct qmi_handle, work);
	struct qmi_ops *ops = &qmi->ops;
	struct sockaddr_qrtr sq;
	struct msghdr msg = { .msg_name = &sq, .msg_namelen = sizeof(sq) };
	struct kvec iv;
	ssize_t msglen;

	for (;;) {
		iv.iov_base = qmi->recv_buf;
		iv.iov_len = qmi->recv_buf_size;

		mutex_lock(&qmi->sock_lock);
		if (qmi->sock)
			msglen = kernel_recvmsg(qmi->sock, &msg, &iv, 1,
						iv.iov_len, MSG_DONTWAIT);
		else
			msglen = -EPIPE;
		mutex_unlock(&qmi->sock_lock);
		if (msglen == -EAGAIN)
			break;

		if (msglen == -ENETRESET) {
			qmi_handle_net_reset(qmi);

			/* The old qmi->sock is gone, our work is done */
			break;
		}

		if (msglen < 0) {
			pr_err("qmi recvmsg failed: %zd\n", msglen);
			break;
		}

		if (sq.sq_node == qmi->sq.sq_node &&
		    sq.sq_port == QRTR_PORT_CTRL) {
			qmi_recv_ctrl_pkt(qmi, qmi->recv_buf, msglen);
		} else if (ops->msg_handler) {
			ops->msg_handler(qmi, &sq, qmi->recv_buf, msglen);
		} else {
			qmi_handle_message(qmi, &sq, qmi->recv_buf, msglen);
		}
	}
}

static void qmi_data_ready(struct sock *sk)
{
	struct qmi_handle *qmi = sk->sk_user_data;

	trace_sk_data_ready(sk);

	/*
	 * This will be NULL if we receive data while being in
	 * qmi_handle_release()
	 */
	if (!qmi)
		return;

	queue_work(qmi->wq, &qmi->work);
}

static struct socket *qmi_sock_create(struct qmi_handle *qmi,
				      struct sockaddr_qrtr *sq)
{
	struct socket *sock;
	int ret;

	ret = sock_create_kern(&init_net, AF_QIPCRTR, SOCK_DGRAM,
			       PF_QIPCRTR, &sock);
	if (ret < 0)
		return ERR_PTR(ret);

	ret = kernel_getsockname(sock, (struct sockaddr *)sq);
	if (ret < 0) {
		sock_release(sock);
		return ERR_PTR(ret);
	}

	sock->sk->sk_user_data = qmi;
	sock->sk->sk_data_ready = qmi_data_ready;
	sock->sk->sk_error_report = qmi_data_ready;

	return sock;
}

/**
 * qmi_handle_init() - initialize a QMI client handle
 * @qmi:	QMI handle to initialize
 * @recv_buf_size: maximum size of incoming message
 * @ops:	reference to callbacks for QRTR notifications
 * @handlers:	NULL-terminated list of QMI message handlers
 *
 * This initializes the QMI client handle to allow sending and receiving QMI
 * messages. As messages are received the appropriate handler will be invoked.
 *
 * Return: 0 on success, negative errno on failure.
 */
int qmi_handle_init(struct qmi_handle *qmi, size_t recv_buf_size,
		    const struct qmi_ops *ops,
		    const struct qmi_msg_handler *handlers)
{
	int ret;

	mutex_init(&qmi->txn_lock);
	mutex_init(&qmi->sock_lock);

	idr_init(&qmi->txns);

	INIT_LIST_HEAD(&qmi->lookups);
	INIT_LIST_HEAD(&qmi->lookup_results);
	INIT_LIST_HEAD(&qmi->services);

	INIT_WORK(&qmi->work, qmi_data_ready_work);

	qmi->handlers = handlers;
	if (ops)
		qmi->ops = *ops;

	/* Make room for the header */
	recv_buf_size += sizeof(struct qmi_header);
	/* Must also be sufficient to hold a control packet */
	if (recv_buf_size < sizeof(struct qrtr_ctrl_pkt))
		recv_buf_size = sizeof(struct qrtr_ctrl_pkt);

	qmi->recv_buf_size = recv_buf_size;
	qmi->recv_buf = kzalloc(recv_buf_size, GFP_KERNEL);
	if (!qmi->recv_buf)
		return -ENOMEM;

	qmi->wq = alloc_ordered_workqueue("qmi_msg_handler", 0);
	if (!qmi->wq) {
		ret = -ENOMEM;
		goto err_free_recv_buf;
	}

	qmi->sock = qmi_sock_create(qmi, &qmi->sq);
	if (IS_ERR(qmi->sock)) {
		if (PTR_ERR(qmi->sock) == -EAFNOSUPPORT) {
			ret = -EPROBE_DEFER;
		} else {
			pr_err("failed to create QMI socket\n");
			ret = PTR_ERR(qmi->sock);
		}
		goto err_destroy_wq;
	}

	return 0;

err_destroy_wq:
	destroy_workqueue(qmi->wq);
err_free_recv_buf:
	kfree(qmi->recv_buf);

	return ret;
}
EXPORT_SYMBOL(qmi_handle_init);

/**
 * qmi_handle_release() - release the QMI client handle
 * @qmi:	QMI client handle
 *
 * This closes the underlying socket and stops any handling of QMI messages.
 */
void qmi_handle_release(struct qmi_handle *qmi)
{
	struct socket *sock = qmi->sock;
	struct qmi_service *svc, *tmp;

	sock->sk->sk_user_data = NULL;
	cancel_work_sync(&qmi->work);

	qmi_recv_del_server(qmi, -1, -1);

	mutex_lock(&qmi->sock_lock);
	sock_release(sock);
	qmi->sock = NULL;
	mutex_unlock(&qmi->sock_lock);

	destroy_workqueue(qmi->wq);

	idr_destroy(&qmi->txns);

	kfree(qmi->recv_buf);

	/* Free registered lookup requests */
	list_for_each_entry_safe(svc, tmp, &qmi->lookups, list_node) {
		list_del(&svc->list_node);
		kfree(svc);
	}

	/* Free registered service information */
	list_for_each_entry_safe(svc, tmp, &qmi->services, list_node) {
		list_del(&svc->list_node);
		kfree(svc);
	}
}
EXPORT_SYMBOL(qmi_handle_release);

/**
 * qmi_send_message() - send a QMI message
 * @qmi:	QMI client handle
 * @sq:		destination sockaddr
 * @txn:	transaction object to use for the message
 * @type:	type of message to send
 * @msg_id:	message id
 * @len:	max length of the QMI message
 * @ei:		QMI message description
 * @c_struct:	object to be encoded
 *
 * This function encodes @c_struct using @ei into a message of type @type,
 * with @msg_id and @txn into a buffer of maximum size @len, and sends this to
 * @sq.
 *
 * Return: 0 on success, negative errno on failure.
 */
static ssize_t qmi_send_message(struct qmi_handle *qmi,
				struct sockaddr_qrtr *sq, struct qmi_txn *txn,
				int type, int msg_id, size_t len,
				const struct qmi_elem_info *ei,
				const void *c_struct)
{
	struct msghdr msghdr = {};
	struct kvec iv;
	void *msg;
	int ret;

	msg = qmi_encode_message(type,
				 msg_id, &len,
				 txn->id, ei,
				 c_struct);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	iv.iov_base = msg;
	iv.iov_len = len;

	if (sq) {
		msghdr.msg_name = sq;
		msghdr.msg_namelen = sizeof(*sq);
	}

	mutex_lock(&qmi->sock_lock);
	if (qmi->sock) {
		ret = kernel_sendmsg(qmi->sock, &msghdr, &iv, 1, len);
		if (ret < 0)
			pr_err("failed to send QMI message\n");
	} else {
		ret = -EPIPE;
	}
	mutex_unlock(&qmi->sock_lock);

	kfree(msg);

	return ret < 0 ? ret : 0;
}

/**
 * qmi_send_request() - send a request QMI message
 * @qmi:	QMI client handle
 * @sq:		destination sockaddr
 * @txn:	transaction object to use for the message
 * @msg_id:	message id
 * @len:	max length of the QMI message
 * @ei:		QMI message description
 * @c_struct:	object to be encoded
 *
 * Return: 0 on success, negative errno on failure.
 */
ssize_t qmi_send_request(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			 struct qmi_txn *txn, int msg_id, size_t len,
			 const struct qmi_elem_info *ei, const void *c_struct)
{
	return qmi_send_message(qmi, sq, txn, QMI_REQUEST, msg_id, len, ei,
				c_struct);
}
EXPORT_SYMBOL(qmi_send_request);

/**
 * qmi_send_response() - send a response QMI message
 * @qmi:	QMI client handle
 * @sq:		destination sockaddr
 * @txn:	transaction object to use for the message
 * @msg_id:	message id
 * @len:	max length of the QMI message
 * @ei:		QMI message description
 * @c_struct:	object to be encoded
 *
 * Return: 0 on success, negative errno on failure.
 */
ssize_t qmi_send_response(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			  struct qmi_txn *txn, int msg_id, size_t len,
			  const struct qmi_elem_info *ei, const void *c_struct)
{
	return qmi_send_message(qmi, sq, txn, QMI_RESPONSE, msg_id, len, ei,
				c_struct);
}
EXPORT_SYMBOL(qmi_send_response);

/**
 * qmi_send_indication() - send an indication QMI message
 * @qmi:	QMI client handle
 * @sq:		destination sockaddr
 * @msg_id:	message id
 * @len:	max length of the QMI message
 * @ei:		QMI message description
 * @c_struct:	object to be encoded
 *
 * Return: 0 on success, negative errno on failure.
 */
ssize_t qmi_send_indication(struct qmi_handle *qmi, struct sockaddr_qrtr *sq,
			    int msg_id, size_t len,
			    const struct qmi_elem_info *ei,
			    const void *c_struct)
{
	struct qmi_txn txn;
	ssize_t rval;
	int ret;

	ret = qmi_txn_init(qmi, &txn, NULL, NULL);
	if (ret < 0)
		return ret;

	rval = qmi_send_message(qmi, sq, &txn, QMI_INDICATION, msg_id, len, ei,
				c_struct);

	/* We don't care about future messages on this txn */
	qmi_txn_cancel(&txn);

	return rval;
}
EXPORT_SYMBOL(qmi_send_indication);

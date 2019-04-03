// SPDX-License-Identifier: GPL-2.0
/**
 * Rockchip rk1608 device driver
 *
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 */
#include <linux/of_platform.h>
#include <linux/ctype.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <linux/compat.h>
#include <linux/rk-preisp.h>
#include "rk1608.h"

#define DEBUG_DUMP_ALL_SEND_RECV_MSG 0

#define MSG_QUEUE_DEFAULT_SIZE (8 * 1024)

struct msg_queue {
	u32 *buf_head; /* msg buffer head */
	u32 *buf_tail; /* msg buffer tail */
	u32 *cur_send; /* current msg send position */
	u32 *cur_recv; /* current msg receive position */
};

struct rk1608_client {
	s8 id;
	struct msg_queue q;
	struct list_head list;
	wait_queue_head_t wait;
	void *private_data;
};

enum {
	AUTO_ARG_TYPE_STR,
	AUTO_ARG_TYPE_INT32,
};

struct auto_arg {
	int type;
	union {
		s32 m_int32;
		const char *m_str;
	};
};

struct auto_args {
	int argc;
	struct auto_arg *argv;
};

/**
 * msq_init - Initialize msg queue
 *
 * @q: the msg queue to initialize
 * @size: size of msg queue buf
 *
 * It returns zero on success, else a negative error code.
 */
static int msq_init(struct msg_queue *q, int size)
{
	u32 *buf = kmalloc(size, GFP_KERNEL);

	q->buf_head = buf;
	q->buf_tail = buf + size / sizeof(u32);
	q->cur_send = buf;
	q->cur_recv = buf;

	return 0;
}

/**
 * msq_release - release msg queue buf
 *
 * @q: the msg queue to release
 */
static void msq_release(struct msg_queue *q)
{
	kfree(q->buf_head);
	q->buf_head = NULL;
	q->buf_tail = NULL;
	q->cur_send = NULL;
	q->cur_recv = NULL;
}

/**
 * msq_is_empty - tests whether a msg queue is empty
 *
 * @q: the msg queue to test
 *
 * It returns true on msg queue is empty, else false.
 */
static int msq_is_empty(const struct msg_queue *q)
{
	return q->cur_send == q->cur_recv;
}

/**
 * msq_tail_free_size - get msg queue tail unused buf size
 *
 * @q: msg queue
 *
 * It returns size of msg queue tail unused buf size, unit 4 bytes
 */
static u32 msq_tail_free_size(const struct msg_queue *q)
{
	if (q->cur_send >= q->cur_recv)
		return (q->buf_tail - q->cur_send);

	return q->cur_recv - q->cur_send;
}

/**
 * msq_head_free_size - get msg queue head unused buf size
 *
 * @q: msg queue
 *
 * It returns size of msg queue head unused buf size, unit 4 bytes
 */
static u32 msq_head_free_size(const struct msg_queue *q)
{
	if (q->cur_send >= q->cur_recv)
		return (q->cur_recv - q->buf_head);

	return 0;
}

/**
 * msq_send_msg - send a msg to msg queue
 *
 * @q: msg queue
 * @m: a msg to queue
 *
 * It returns zero on success, else a negative error code.
 */
static int msq_send_msg(struct msg_queue *q, const struct msg *m)
{
	int ret = 0;

	if (msq_tail_free_size(q) > m->size) {
		u32 *next_send;

		memcpy(q->cur_send, m, m->size * sizeof(u32));
		next_send = q->cur_send + m->size;
		if (next_send == q->buf_tail)
			next_send = q->buf_head;

		q->cur_send = next_send;
	} else if (msq_head_free_size(q) > m->size) {
		*q->cur_send = 0; /* set size to 0 for skip to head mark */
		memcpy(q->buf_head, m, m->size * sizeof(u32));
		q->cur_send = q->buf_head + m->size;
	} else {
		ret = -1;
	}

	return ret;
}

/**
 * msq_recv_msg - receive a msg from msg queue
 *
 * @q: msg queue
 * @m: a msg pointer buf [out]
 *
 * need call msq_recv_msg_free to free msg after msg use done
 *
 * It returns zero on success, else a negative error code.
 */
static int msq_recv_msg(struct msg_queue *q, struct msg **m)
{
	*m = NULL;
	if (msq_is_empty(q))
		return -1;

	/* skip to head when size is 0 */
	if (*q->cur_recv == 0)
		*m = (struct msg *)q->buf_head;
	else
		*m = (struct msg *)q->cur_recv;

	return 0;
}

/**
 * msq_free_received_msg - free a received msg to msg queue
 *
 * @q: msg queue
 * @m: a msg
 *
 * It returns zero on success, else a negative error code.
 */
static int msq_free_received_msg(struct msg_queue *q, const struct msg *m)
{
	/* skip to head when size is 0 */
	if (*q->cur_recv == 0) {
		q->cur_recv = q->buf_head + m->size;
	} else {
		u32 *next_recv;

		next_recv = q->cur_recv + m->size;
		if (next_recv == q->buf_tail)
			next_recv = q->buf_head;

		q->cur_recv = next_recv;
	}

	return 0;
}

static void rk1608_client_list_init(struct rk1608_client_list *s)
{
	mutex_init(&s->mutex);
	INIT_LIST_HEAD(&s->list);
}

static struct rk1608_client *rk1608_client_new(void)
{
	struct rk1608_client *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return NULL;

	c->id = INVALID_ID;
	INIT_LIST_HEAD(&c->list);
	msq_init(&c->q, MSG_QUEUE_DEFAULT_SIZE);
	init_waitqueue_head(&c->wait);

	return c;
}

static void rk1608_client_release(struct rk1608_client *c)
{
	msq_release(&c->q);
	kfree(c);
}

static struct rk1608_client *
rk1608_client_find(struct rk1608_client_list *s,
		   struct rk1608_client *c)
{
	struct rk1608_client *client = NULL;

	list_for_each_entry(client, &s->list, list) {
		if (c == client)
			return c;
	}

	return NULL;
}

static int rk1608_client_connect(struct rk1608_state *pdata,
				 struct rk1608_client *c)
{
	struct rk1608_client_list *s = &pdata->clients;

	mutex_lock(&s->mutex);
	if (rk1608_client_find(s, c)) {
		mutex_unlock(&s->mutex);
		return -1;
	}

	list_add_tail(&c->list, &s->list);
	mutex_unlock(&s->mutex);

	return 0;
}

static void rk1608_client_disconnect(struct rk1608_state *pdata,
				     struct rk1608_client *c)
{
	struct rk1608_client_list *s = &pdata->clients;

	mutex_lock(&s->mutex);
	if (rk1608_client_find(s, c))
		list_del_init(&c->list);
	mutex_unlock(&s->mutex);
}

static int parse_arg(const char *s, struct auto_arg *arg)
{
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		long v;

		v = simple_strtol(s, NULL, 16);
		arg->type = AUTO_ARG_TYPE_INT32;
		arg->m_int32 = v;
	} else if (isdigit(s[0])) {
		long v;

		v = simple_strtol(s, NULL, 10);
		arg->type = AUTO_ARG_TYPE_INT32;
		arg->m_int32 = v;
	} else {
		arg->type = AUTO_ARG_TYPE_STR;
		arg->m_str = s;
	}

	return 0;
}

static int parse_auto_args(char *s, struct auto_args *args)
{
	int i = 0;
	char c = 0;
	int last_is_arg_flag = 0;
	const char *last_arg;

	args->argc = 0;

	i = -1;
	do {
		c = s[++i];
		if (c == ' ' || c == ',' || c == '\n' || c == '\r' || c == 0) {
			if (last_is_arg_flag)
				args->argc++;

			last_is_arg_flag = 0;
		} else {
			last_is_arg_flag = 1;
		}
	} while (c != 0 && c != '\n' && c != '\r');

	args->argv =
		kmalloc_array(args->argc, sizeof(struct auto_arg), GFP_KERNEL);
	if (!args->argv)
		return -ENOMEM;

	i = -1;
	last_is_arg_flag = 0;
	last_arg = s;
	args->argc = 0;
	do {
		c = s[++i];
		if (c == ' ' || c == ',' || c == '\n' || c == '\r' || c == 0) {
			if (last_is_arg_flag) {
				parse_arg(last_arg, args->argv + args->argc++);
				s[i] = 0;
			}
			last_is_arg_flag = 0;
		} else {
			if (last_is_arg_flag == 0)
				last_arg = s + i;

			last_is_arg_flag = 1;
		}
	} while (c != 0 && c != '\n' && c != '\r');

	return c == 0 ? i : i + 1;
}

static void free_auto_args(struct auto_args *args)
{
	kfree(args->argv);
	args->argv = NULL;
	args->argc = 0;
}

static void int32_hexdump(const char *prefix, s32 *data, int len)
{
	pr_err("%s\n", prefix);
	print_hex_dump(KERN_ERR, "offset ", DUMP_PREFIX_OFFSET,
		       16, 4, data, len, false);
	pr_err("\n");
}

static int do_cmd_write(struct rk1608_state *pdata,
			const struct auto_args *args)
{
	s32 addr;
	s32 len = (args->argc - 2) * sizeof(s32);
	s32 *data;
	int i;

	if (args->argc < 3 || args->argv[1].type != AUTO_ARG_TYPE_INT32) {
		dev_err(pdata->dev, "Mis or unknown args!\n");
		return -1;
	}

	len = MIN(len, RK1608_MAX_OP_BYTES);

	addr = args->argv[1].m_int32;
	data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < len / 4; i++) {
		if (args->argv[i + 2].type != AUTO_ARG_TYPE_INT32) {
			dev_err(pdata->dev, "Unknown args!\n");
			kfree(data);
			return -1;
		}

		data[i] = args->argv[i + 2].m_int32;
	}

	rk1608_write(pdata->spi, addr, data, len);

	kfree(data);

	dev_info(pdata->dev, "write addr: 0x%x, len: %d bytes\n", addr, len);
	return 0;
}

static int do_cmd_read(struct rk1608_state *pdata,
		       const struct auto_args *args)
{
	s32 addr;
	s32 len;
	s32 *data;

	if (args->argc < 3 || args->argv[1].type != AUTO_ARG_TYPE_INT32) {
		dev_err(pdata->dev, "Mis or unknown args!\n");
		return -1;
	}

	addr = args->argv[1].m_int32;
	if (args->argc == 2) {
		len = 32;
	} else {
		if (args->argv[2].type != AUTO_ARG_TYPE_INT32) {
			dev_err(pdata->dev, "Unknown args!\n");
			return -1;
		}
		len = args->argv[2].m_int32 * sizeof(s32);
		len = MIN(len, RK1608_MAX_OP_BYTES);
	}

	data = kmalloc(len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_info(pdata->dev, "\nread addr: %x, len: %d bytes\n", addr, len);
	rk1608_read(pdata->spi, addr, data, len);
	int32_hexdump("read data:", data, len);
	kfree(data);

	return 0;
}

static int do_cmd_set_spi_rate(struct rk1608_state *pdata,
			       const struct auto_args *args)
{
	if (args->argc < 2 || args->argv[1].type != AUTO_ARG_TYPE_INT32) {
		dev_err(pdata->dev, "Mis or unknown args!\n");
		return -1;
	}

	pdata->max_speed_hz = args->argv[1].m_int32;
	dev_info(pdata->dev, "set spi max speed to %d!\n", pdata->max_speed_hz);

	if (args->argc == 3 && args->argv[2].type == AUTO_ARG_TYPE_INT32) {
		pdata->min_speed_hz = args->argv[2].m_int32;
		dev_info(pdata->dev, "set spi min speed to %d!\n",
			 pdata->min_speed_hz);
	}

	return 0;
}

static int do_cmd_query(struct rk1608_state *pdata,
			const struct auto_args *args)
{
	s32 state;

	rk1608_operation_query(pdata->spi, &state);
	dev_info(pdata->dev, "state %x\n", state);
	return 0;
}

static int do_cmd_download_fw(struct rk1608_state *pdata,
			      const struct auto_args *args)
{
	int ret = 0;
	const char *fw_name = NULL;

	if (args->argc == 2 && args->argv[1].type == AUTO_ARG_TYPE_STR)
		fw_name = args->argv[1].m_str;

	ret = rk1608_download_fw(pdata->spi, fw_name);
	if (ret)
		dev_err(pdata->dev, "download firmware failed!\n");
	else
		dev_info(pdata->dev, "download firmware success!\n");

	return 0;
}

static int do_cmd_fast_write(struct rk1608_state *pdata,
			     const struct auto_args *args)
{
	int ret = 0;
	s32 reg;

	if (args->argc != 2 || args->argv[1].type != AUTO_ARG_TYPE_INT32) {
		dev_err(pdata->dev, "Mis or unknown args!\n");
		return -1;
	}

	reg = args->argv[1].m_int32;

	ret = rk1608_interrupt_request(pdata->spi, reg);
	dev_info(pdata->dev, "interrupt request reg1:%x ret:%x\n", reg, ret);

	return 0;
}

static int do_cmd_fast_read(struct rk1608_state *pdata,
			    const struct auto_args *args)
{
	s32 state;

	rk1608_state_query(pdata->spi, &state);
	dev_info(pdata->dev, "dsp state %x\n", state);

	return 0;
}

static int do_cmd_send_msg(struct rk1608_state *pdata,
			   const struct auto_args *args)
{
	struct msg *m;
	int ret = 0;
	int msg_len;
	u32 i = 0;

	if (args->argc < 2) {
		dev_err(pdata->dev, "need more args\n");
		return -1;
	}

	msg_len = args->argc * sizeof(u32);

	m = kmalloc(msg_len, GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->size = msg_len / 4;
	for (i = 1; i < m->size; i++) {
		if (args->argv[i].type != AUTO_ARG_TYPE_INT32) {
			dev_err(pdata->dev, "Unknown args!\n");
			kfree(m);
			return -1;
		}

		*((s32 *)m + i) = args->argv[i].m_int32;
	}

	ret = rk1608_send_msg_to_dsp(pdata, m);

	dev_info(pdata->dev, "send msg len: %d, ret: %x\n", m->size, ret);

	kfree(m);

	return 0;
}

static int do_cmd_recv_msg(struct rk1608_state *pdata,
			   const struct auto_args *args)
{
	struct msg *m;
	char buf[256] = "";
	int ret = 0;

	ret = rk1608_msq_recv_msg(pdata->spi, &m);
	if (ret || !m)
		return 0;

	dev_info(pdata->dev, "\nrecv msg len: %d, ret: %x\n", m->size, ret);
	int32_hexdump("recv msg:", (s32 *)m, m->size * 4);

	dev_info(pdata->dev, buf);

	kfree(m);

	return 0;
}

static int do_cmd_power_on(struct rk1608_state *pdata,
			   const struct auto_args *args)
{
	int ret;

	ret = rk1608_set_power(pdata, 1);
	dev_info(pdata->dev, "do cmd power on, count++\n");

	return ret;
}

static int do_cmd_power_off(struct rk1608_state *pdata,
			    const struct auto_args *args)
{
	int ret;

	ret = rk1608_set_power(pdata, 0);
	dev_info(pdata->dev, "do cmd power off, count--\n");

	return ret;
}

static int do_cmd_set_dsp_log_level(struct rk1608_state *pdata,
				    const struct auto_args *args)
{
	int ret;

	if (args->argc != 2 || args->argv[1].type != AUTO_ARG_TYPE_INT32) {
		dev_err(pdata->dev, "Mis or unknown args!\n");
		return -1;
	}

	pdata->log_level = args->argv[1].m_int32;
	ret = rk1608_set_log_level(pdata, pdata->log_level);

	dev_info(pdata->dev, "set dsp log level %d, ret: %d\n",
		 pdata->log_level, ret);

	return ret;
}

static int do_cmd_version(struct rk1608_state *pdata,
			  const struct auto_args *args)
{
	dev_info(pdata->dev, "driver version: v%02x.%02x.%02x\n",
		 RK1608_VERSION >> 16,
		 (RK1608_VERSION & 0xff00) >> 8,
		 RK1608_VERSION & 0x00ff);
	return 0;
}

static int do_cmd_help(struct rk1608_state *pdata)
{
	dev_info(pdata->dev, "\n"
		 "support debug commands:\n"
		 "v            -- rk1608 driver version.\n"
		 "log level    -- set rk1608 log level.\n"
		 "on           -- power count + 1.\n"
		 "off          -- power count - 1.\n"
		 "f [fw_name]  -- download fw.\n"
		 "q            -- query operation status.\n"
		 "r addr [length=32] -- read addr.\n"
		 "w addr value,...   -- write addr.\n"
		 "s type,...         -- send msg.\n"
		 "rate max [min]     -- set spi speed.\n\n");
	return 0;
}

static int do_cmd(struct rk1608_state *pdata, const struct auto_args *args)
{
	const char *s;

	if (args->argv->type != AUTO_ARG_TYPE_STR)
		return 0;

	s = args->argv->m_str;
	/* echo c > /dev/rk_preisp */
	if (!strcmp(s, "c"))
		return do_cmd_recv_msg(pdata, args);
	/* echo f [fw_name] > /dev/rk_preisp */
	if (!strcmp(s, "f"))
		return do_cmd_download_fw(pdata, args);
	/* echo fw reg1 > /dev/rk_preisp */
	if (!strcmp(s, "fw"))
		return do_cmd_fast_write(pdata, args);
	/* echo fr > /dev/rk_preisp */
	if (!strcmp(s, "fr"))
		return do_cmd_fast_read(pdata, args);
	/* echo log level > /dev/rk_preisp */
	if (!strcmp(s, "log"))
		return do_cmd_set_dsp_log_level(pdata, args);
	/* echo on > /dev/rk_preisp */
	if (!strcmp(s, "on"))
		return do_cmd_power_on(pdata, args);
	/* echo off > /dev/rk_preisp */
	if (!strcmp(s, "off"))
		return do_cmd_power_off(pdata, args);
	/* echo q > /dev/rk_preisp */
	if (!strcmp(s, "q"))
		return do_cmd_query(pdata, args);
	/* echo r addr [length] > /dev/rk_preisp */
	if (!strcmp(s, "r"))
		return do_cmd_read(pdata, args);
	/* echo rate > /dev/rk_preisp */
	if (!strcmp(s, "rate"))
		return do_cmd_set_spi_rate(pdata, args);
	/* echo s type,... > /dev/rk_preisp */
	if (!strcmp(s, "s"))
		return do_cmd_send_msg(pdata, args);
	/* echo v > /dev/rk_preisp */
	if (!strcmp(s, "v"))
		return do_cmd_version(pdata, args);
	/* echo w addr value,... > /dev/rk_preisp */
	if (!strcmp(s, "w"))
		return do_cmd_write(pdata, args);

	dev_err(pdata->dev, "unknown commands:%s\n", s);
	do_cmd_help(pdata);

	return 0;
}

static int rk1608_dev_open(struct inode *inode, struct file *file)
{
	struct rk1608_state *pdata =
		container_of(file->private_data, struct rk1608_state, misc);
	struct rk1608_client *client = rk1608_client_new();

	client->private_data = pdata;
	file->private_data = client;

	rk1608_set_power(pdata, 1);

	return 0;
}

static int rk1608_dev_release(struct inode *inode, struct file *file)
{
	struct rk1608_client *client = file->private_data;
	struct rk1608_state *pdata = client->private_data;

	rk1608_client_disconnect(pdata, client);
	rk1608_client_release(client);
	rk1608_set_power(pdata, 0);

	return 0;
}

static ssize_t rk1608_dev_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	char *buf;
	struct auto_args args;
	int i;
	struct rk1608_client *client = file->private_data;
	struct rk1608_state *pdata = client->private_data;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = 0;

	i = 0;
	while (buf[i] != 0) {
		int ret = parse_auto_args(buf + i, &args);

		if (ret < 0)
			break;

		i += ret;
		if (args.argc == 0)
			continue;

		do_cmd(pdata, &args);
		free_auto_args(&args);
	}

	kfree(buf);

	return count;
}

static long rk1608_dev_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	int ret = 0;
	void __user *ubuf = (void __user *)arg;
	struct rk1608_client *client = file->private_data;
	struct rk1608_state *pdata = client->private_data;

	switch (cmd) {
	case PREISP_POWER_ON:
		ret = rk1608_set_power(pdata, 1);
		break;
	case PREISP_POWER_OFF:
		ret = rk1608_set_power(pdata, 0);
		break;
	case PREISP_DOWNLOAD_FW: {
		char fw_name[PREISP_FW_NAME_LEN];

		if (strncpy_from_user(fw_name, ubuf, PREISP_FW_NAME_LEN) <= 0) {
			ret = -EFAULT;
			break;
		}
		dev_info(pdata->dev, "download fw:%s\n", fw_name);
		ret = rk1608_download_fw(pdata->spi, fw_name);
		break;
	}
	case PREISP_WRITE: {
		struct preisp_apb_pkt pkt;
		s32 *data;

		if (copy_from_user(&pkt, ubuf, sizeof(pkt))) {
			ret = -EFAULT;
			break;
		}
		pkt.data_len = MIN(pkt.data_len, RK1608_MAX_OP_BYTES);
		data = memdup_user((void __user *)pkt.data, pkt.data_len);
		if (IS_ERR(data)) {
			ret = (long)ERR_PTR((long)data);
			break;
		}

		ret = rk1608_safe_write(pdata->spi, pkt.addr, data, pkt.data_len);
		kfree(data);
		break;
	}
	case PREISP_READ: {
		struct preisp_apb_pkt pkt;
		s32 *data;

		if (copy_from_user(&pkt, ubuf, sizeof(pkt))) {
			ret = -EFAULT;
			break;
		}
		pkt.data_len = MIN(pkt.data_len, RK1608_MAX_OP_BYTES);
		data = kmalloc(pkt.data_len, GFP_KERNEL);
		if (!data) {
			ret = -ENOMEM;
			break;
		}

		ret = rk1608_safe_read(pdata->spi, pkt.addr, data, pkt.data_len);
		if (ret) {
			kfree(data);
			break;
		}
		ret = copy_to_user((void __user *)pkt.data, data, pkt.data_len);

		kfree(data);
		break;
	}
	case PREISP_ST_QUERY: {
		s32 state;

		ret = rk1608_state_query(pdata->spi, &state);
		if (ret)
			break;

		ret = put_user(state, (s32 __user *)ubuf);
		break;
	}
	case PREISP_IRQ_REQUEST: {
		int int_num = arg;

		ret = rk1608_interrupt_request(pdata->spi, int_num);
		break;
	}
	case PREISP_SEND_MSG: {
		struct msg *msg;
		u32 len;

		if (get_user(len, (u32 __user *)ubuf)) {
			ret = -EFAULT;
			break;
		}
		len = len * sizeof(s32);
		msg = memdup_user(ubuf, len);
		if (IS_ERR(msg)) {
			ret = (long)ERR_PTR((long)msg);
			break;
		}

#if DEBUG_DUMP_ALL_SEND_RECV_MSG == 1
		int32_hexdump("send msg:", (s32 *)msg, len);
#endif

		ret = rk1608_send_msg_to_dsp(pdata, msg);
		kfree(msg);
		break;
	}
	case PREISP_QUERY_MSG: {
		struct msg *msg;

		ret = msq_recv_msg(&client->q, &msg);
		if (ret)
			break;

		ret = put_user(msg->size, (u32 __user *)ubuf);
		break;
	}
	case PREISP_RECV_MSG: {
		struct msg *msg;

		ret = msq_recv_msg(&client->q, &msg);
		if (ret)
			break;

		ret = copy_to_user(ubuf, msg, msg->size * sizeof(u32));
		msq_free_received_msg(&client->q, msg);
		break;
	}
	case PREISP_CLIENT_CONNECT: {
		client->id = (int)arg;
		ret = rk1608_client_connect(pdata, client);
		break;
	}
	case PREISP_CLIENT_DISCONNECT: {
		rk1608_client_disconnect(pdata, client);
		client->id = INVALID_ID;
		break;
	}
	default:
		ret = -EFAULT;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
#define PREISP_WRITE32		_IOW('p', 6, struct preisp_apb_pkt32)
#define PREISP_READ32		_IOR('p', 7, struct preisp_apb_pkt32)

struct preisp_apb_pkt32 {
	s32 data_len;
	s32 addr;
	compat_uptr_t data;
};

static long rk1608_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;
	struct preisp_apb_pkt32 pkt32;
	struct preisp_apb_pkt *pkt;

	switch (cmd) {
	case PREISP_WRITE32:
		cmd = PREISP_WRITE;
		break;
	case PREISP_READ32:
		cmd = PREISP_READ;
		break;
	default:
		break;
	}

	switch (cmd) {
	case PREISP_WRITE:
	case PREISP_READ:
		if (copy_from_user(&pkt32, (void __user *)arg, sizeof(pkt32)))
			return -EFAULT;

		pkt = compat_alloc_user_space(sizeof(*pkt));
		if (!pkt ||
		    put_user(pkt32.data_len, &pkt->data_len) ||
		    put_user(pkt32.addr, &pkt->addr) ||
		    put_user(compat_ptr(pkt32.data), &pkt->data))
			return -EFAULT;

		ret = rk1608_dev_ioctl(file, cmd, (unsigned long)pkt);
		break;
	default:
		ret = rk1608_dev_ioctl(file, cmd, arg);
		break;
	}

	return ret;
}
#endif

static unsigned int rk1608_dev_poll(struct file *file, poll_table *wait)
{
	struct rk1608_client *client = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &client->wait, wait);

	if (!msq_is_empty(&client->q))
		mask |= POLLIN;

	return mask;
}

static const struct file_operations rk1608_fops = {
	.owner = THIS_MODULE,
	.open = rk1608_dev_open,
	.release = rk1608_dev_release,
	.write = rk1608_dev_write,
	.poll = rk1608_dev_poll,
	.unlocked_ioctl = rk1608_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = rk1608_compat_ioctl,
#endif
};

void rk1608_dev_receive_msg(struct rk1608_state *pdata, struct msg *msg)
{
	struct rk1608_client_list *s = &pdata->clients;
	struct rk1608_client *client;

#if DEBUG_DUMP_ALL_SEND_RECV_MSG == 1
	int32_hexdump("recv msg:", (s32 *)msg, msg->size * 4);
#endif

	mutex_lock(&s->mutex);
	list_for_each_entry(client, &s->list, list) {
		if (client->id == msg->id.camera_id) {
			msq_send_msg(&client->q, msg);
			wake_up_interruptible(&client->wait);
		}
	}
	mutex_unlock(&s->mutex);
}

int rk1608_dev_register(struct rk1608_state *pdata)
{
	int ret;

	rk1608_client_list_init(&pdata->clients);

	pdata->misc.minor = MISC_DYNAMIC_MINOR;
	pdata->misc.name = "rk_preisp";
	pdata->misc.fops = &rk1608_fops;

	ret = misc_register(&pdata->misc);
	if (ret < 0)
		dev_err(pdata->dev, "Error: misc_register returned %d\n", ret);

	return 0;
}

void rk1608_dev_unregister(struct rk1608_state *pdata)
{
	misc_deregister(&pdata->misc);
}

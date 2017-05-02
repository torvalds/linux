/*
 * Copyright (C) STMicroelectronics SA 2015
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/rpmsg.h>

#include "delta.h"
#include "delta-ipc.h"
#include "delta-mem.h"

#define IPC_TIMEOUT 100
#define IPC_SANITY_TAG 0xDEADBEEF

enum delta_ipc_fw_command {
	DELTA_IPC_OPEN,
	DELTA_IPC_SET_STREAM,
	DELTA_IPC_DECODE,
	DELTA_IPC_CLOSE
};

#define to_rpmsg_driver(__drv) container_of(__drv, struct rpmsg_driver, drv)
#define to_delta(__d) container_of(__d, struct delta_dev, rpmsg_driver)

#define to_ctx(hdl) ((struct delta_ipc_ctx *)hdl)
#define to_pctx(ctx) container_of(ctx, struct delta_ctx, ipc_ctx)

struct delta_ipc_header_msg {
	u32 tag;
	void *host_hdl;
	u32 copro_hdl;
	u32 command;
};

#define to_host_hdl(ctx) ((void *)ctx)

#define msg_to_ctx(msg) ((struct delta_ipc_ctx *)(msg)->header.host_hdl)
#define msg_to_copro_hdl(msg) ((msg)->header.copro_hdl)

static inline dma_addr_t to_paddr(struct delta_ipc_ctx *ctx, void *vaddr)
{
	return (ctx->ipc_buf->paddr + (vaddr - ctx->ipc_buf->vaddr));
}

static inline bool is_valid_data(struct delta_ipc_ctx *ctx,
				 void *data, u32 size)
{
	return ((data >= ctx->ipc_buf->vaddr) &&
		((data + size) <= (ctx->ipc_buf->vaddr + ctx->ipc_buf->size)));
}

/*
 * IPC shared memory (@ipc_buf_size, @ipc_buf_paddr) is sent to copro
 * at each instance opening. This memory is allocated by IPC client
 * and given through delta_ipc_open(). All messages parameters
 * (open, set_stream, decode) will have their phy address within
 * this IPC shared memory, avoiding de-facto recopies inside delta-ipc.
 * All the below messages structures are used on both host and firmware
 * side and are packed (use only of 32 bits size fields in messages
 * structures to ensure packing):
 * - struct delta_ipc_open_msg
 * - struct delta_ipc_set_stream_msg
 * - struct delta_ipc_decode_msg
 * - struct delta_ipc_close_msg
 * - struct delta_ipc_cb_msg
 */
struct delta_ipc_open_msg {
	struct delta_ipc_header_msg header;
	u32 ipc_buf_size;
	dma_addr_t ipc_buf_paddr;
	char name[32];
	u32 param_size;
	dma_addr_t param_paddr;
};

struct delta_ipc_set_stream_msg {
	struct delta_ipc_header_msg header;
	u32 param_size;
	dma_addr_t param_paddr;
};

struct delta_ipc_decode_msg {
	struct delta_ipc_header_msg header;
	u32 param_size;
	dma_addr_t param_paddr;
	u32 status_size;
	dma_addr_t status_paddr;
};

struct delta_ipc_close_msg {
	struct delta_ipc_header_msg header;
};

struct delta_ipc_cb_msg {
	struct delta_ipc_header_msg header;
	int err;
};

static void build_msg_header(struct delta_ipc_ctx *ctx,
			     enum delta_ipc_fw_command command,
			     struct delta_ipc_header_msg *header)
{
	header->tag = IPC_SANITY_TAG;
	header->host_hdl = to_host_hdl(ctx);
	header->copro_hdl = ctx->copro_hdl;
	header->command = command;
}

int delta_ipc_open(struct delta_ctx *pctx, const char *name,
		   struct delta_ipc_param *param, u32 ipc_buf_size,
		   struct delta_buf **ipc_buf, void **hdl)
{
	struct delta_dev *delta = pctx->dev;
	struct rpmsg_device *rpmsg_device = delta->rpmsg_device;
	struct delta_ipc_ctx *ctx = &pctx->ipc_ctx;
	struct delta_ipc_open_msg msg;
	struct delta_buf *buf = &ctx->ipc_buf_struct;
	int ret;

	if (!rpmsg_device) {
		dev_err(delta->dev,
			"%s   ipc: failed to open, rpmsg is not initialized\n",
			pctx->name);
		pctx->sys_errors++;
		return -EINVAL;
	}

	if (!name) {
		dev_err(delta->dev,
			"%s   ipc: failed to open, no name given\n",
			pctx->name);
		return -EINVAL;
	}

	if (!param || !param->data || !param->size) {
		dev_err(delta->dev,
			"%s  ipc: failed to open, empty parameter\n",
			pctx->name);
		return -EINVAL;
	}

	if (!ipc_buf_size) {
		dev_err(delta->dev,
			"%s   ipc: failed to open, no size given for ipc buffer\n",
			pctx->name);
		return -EINVAL;
	}

	if (param->size > ipc_buf_size) {
		dev_err(delta->dev,
			"%s   ipc: failed to open, too large ipc parameter (%d bytes while max %d expected)\n",
			pctx->name,
			param->size, ctx->ipc_buf->size);
		return -EINVAL;
	}

	/* init */
	init_completion(&ctx->done);

	/*
	 * allocation of contiguous buffer for
	 * data of commands exchanged between
	 * host and firmware coprocessor
	 */
	ret = hw_alloc(pctx, ipc_buf_size,
		       "ipc data buffer", buf);
	if (ret)
		return ret;
	ctx->ipc_buf = buf;

	/* build rpmsg message */
	build_msg_header(ctx, DELTA_IPC_OPEN, &msg.header);

	msg.ipc_buf_size = ipc_buf_size;
	msg.ipc_buf_paddr = ctx->ipc_buf->paddr;

	memcpy(msg.name, name, sizeof(msg.name));
	msg.name[sizeof(msg.name) - 1] = 0;

	msg.param_size = param->size;
	memcpy(ctx->ipc_buf->vaddr, param->data, msg.param_size);
	msg.param_paddr = ctx->ipc_buf->paddr;

	/* send it */
	ret = rpmsg_send(rpmsg_device->ept, &msg, sizeof(msg));
	if (ret) {
		dev_err(delta->dev,
			"%s   ipc: failed to open, rpmsg_send failed (%d) for DELTA_IPC_OPEN (name=%s, size=%d, data=%p)\n",
			pctx->name,
			ret, name, param->size, param->data);
		goto err;
	}

	/* wait for acknowledge */
	if (!wait_for_completion_timeout
	    (&ctx->done, msecs_to_jiffies(IPC_TIMEOUT))) {
		dev_err(delta->dev,
			"%s   ipc: failed to open, timeout waiting for DELTA_IPC_OPEN callback (name=%s, size=%d, data=%p)\n",
			pctx->name,
			name, param->size, param->data);
		ret = -ETIMEDOUT;
		goto err;
	}

	/* command completed, check error */
	if (ctx->cb_err) {
		dev_err(delta->dev,
			"%s   ipc: failed to open, DELTA_IPC_OPEN completed but with error (%d) (name=%s, size=%d, data=%p)\n",
			pctx->name,
			ctx->cb_err, name, param->size, param->data);
		ret = -EIO;
		goto err;
	}

	*ipc_buf = ctx->ipc_buf;
	*hdl = (void *)ctx;

	return 0;

err:
	pctx->sys_errors++;
	if (ctx->ipc_buf) {
		hw_free(pctx, ctx->ipc_buf);
		ctx->ipc_buf = NULL;
	}

	return ret;
};

int delta_ipc_set_stream(void *hdl, struct delta_ipc_param *param)
{
	struct delta_ipc_ctx *ctx = to_ctx(hdl);
	struct delta_ctx *pctx = to_pctx(ctx);
	struct delta_dev *delta = pctx->dev;
	struct rpmsg_device *rpmsg_device = delta->rpmsg_device;
	struct delta_ipc_set_stream_msg msg;
	int ret;

	if (!hdl) {
		dev_err(delta->dev,
			"%s   ipc: failed to set stream, invalid ipc handle\n",
			pctx->name);
		return -EINVAL;
	}

	if (!rpmsg_device) {
		dev_err(delta->dev,
			"%s   ipc: failed to set stream, rpmsg is not initialized\n",
			pctx->name);
		return -EINVAL;
	}

	if (!param || !param->data || !param->size) {
		dev_err(delta->dev,
			"%s  ipc: failed to set stream, empty parameter\n",
			pctx->name);
		return -EINVAL;
	}

	if (param->size > ctx->ipc_buf->size) {
		dev_err(delta->dev,
			"%s   ipc: failed to set stream, too large ipc parameter(%d bytes while max %d expected)\n",
			pctx->name,
			param->size, ctx->ipc_buf->size);
		return -EINVAL;
	}

	if (!is_valid_data(ctx, param->data, param->size)) {
		dev_err(delta->dev,
			"%s   ipc: failed to set stream, parameter is not in expected address range (size=%d, data=%p not in %p..%p)\n",
			pctx->name,
			param->size,
			param->data,
			ctx->ipc_buf->vaddr,
			ctx->ipc_buf->vaddr + ctx->ipc_buf->size - 1);
		return -EINVAL;
	}

	/* build rpmsg message */
	build_msg_header(ctx, DELTA_IPC_SET_STREAM, &msg.header);

	msg.param_size = param->size;
	msg.param_paddr = to_paddr(ctx, param->data);

	/* send it */
	ret = rpmsg_send(rpmsg_device->ept, &msg, sizeof(msg));
	if (ret) {
		dev_err(delta->dev,
			"%s   ipc: failed to set stream, rpmsg_send failed (%d) for DELTA_IPC_SET_STREAM (size=%d, data=%p)\n",
			pctx->name,
			ret, param->size, param->data);
		pctx->sys_errors++;
		return ret;
	}

	/* wait for acknowledge */
	if (!wait_for_completion_timeout
	    (&ctx->done, msecs_to_jiffies(IPC_TIMEOUT))) {
		dev_err(delta->dev,
			"%s   ipc: failed to set stream, timeout waiting for DELTA_IPC_SET_STREAM callback (size=%d, data=%p)\n",
			pctx->name,
			param->size, param->data);
		pctx->sys_errors++;
		return -ETIMEDOUT;
	}

	/* command completed, check status */
	if (ctx->cb_err) {
		dev_err(delta->dev,
			"%s   ipc: failed to set stream, DELTA_IPC_SET_STREAM completed but with error (%d) (size=%d, data=%p)\n",
			pctx->name,
			ctx->cb_err, param->size, param->data);
		pctx->sys_errors++;
		return -EIO;
	}

	return 0;
}

int delta_ipc_decode(void *hdl, struct delta_ipc_param *param,
		     struct delta_ipc_param *status)
{
	struct delta_ipc_ctx *ctx = to_ctx(hdl);
	struct delta_ctx *pctx = to_pctx(ctx);
	struct delta_dev *delta = pctx->dev;
	struct rpmsg_device *rpmsg_device = delta->rpmsg_device;
	struct delta_ipc_decode_msg msg;
	int ret;

	if (!hdl) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, invalid ipc handle\n",
			pctx->name);
		return -EINVAL;
	}

	if (!rpmsg_device) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, rpmsg is not initialized\n",
			pctx->name);
		return -EINVAL;
	}

	if (!param || !param->data || !param->size) {
		dev_err(delta->dev,
			"%s  ipc: failed to decode, empty parameter\n",
			pctx->name);
		return -EINVAL;
	}

	if (!status || !status->data || !status->size) {
		dev_err(delta->dev,
			"%s  ipc: failed to decode, empty status\n",
			pctx->name);
		return -EINVAL;
	}

	if (param->size + status->size > ctx->ipc_buf->size) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, too large ipc parameter (%d bytes (param) + %d bytes (status) while max %d expected)\n",
			pctx->name,
			param->size,
			status->size,
			ctx->ipc_buf->size);
		return -EINVAL;
	}

	if (!is_valid_data(ctx, param->data, param->size)) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, parameter is not in expected address range (size=%d, data=%p not in %p..%p)\n",
			pctx->name,
			param->size,
			param->data,
			ctx->ipc_buf->vaddr,
			ctx->ipc_buf->vaddr + ctx->ipc_buf->size - 1);
		return -EINVAL;
	}

	if (!is_valid_data(ctx, status->data, status->size)) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, status is not in expected address range (size=%d, data=%p not in %p..%p)\n",
			pctx->name,
			status->size,
			status->data,
			ctx->ipc_buf->vaddr,
			ctx->ipc_buf->vaddr + ctx->ipc_buf->size - 1);
		return -EINVAL;
	}

	/* build rpmsg message */
	build_msg_header(ctx, DELTA_IPC_DECODE, &msg.header);

	msg.param_size = param->size;
	msg.param_paddr = to_paddr(ctx, param->data);

	msg.status_size = status->size;
	msg.status_paddr = to_paddr(ctx, status->data);

	/* send it */
	ret = rpmsg_send(rpmsg_device->ept, &msg, sizeof(msg));
	if (ret) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, rpmsg_send failed (%d) for DELTA_IPC_DECODE (size=%d, data=%p)\n",
			pctx->name,
			ret, param->size, param->data);
		pctx->sys_errors++;
		return ret;
	}

	/* wait for acknowledge */
	if (!wait_for_completion_timeout
	    (&ctx->done, msecs_to_jiffies(IPC_TIMEOUT))) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, timeout waiting for DELTA_IPC_DECODE callback (size=%d, data=%p)\n",
			pctx->name,
			param->size, param->data);
		pctx->sys_errors++;
		return -ETIMEDOUT;
	}

	/* command completed, check status */
	if (ctx->cb_err) {
		dev_err(delta->dev,
			"%s   ipc: failed to decode, DELTA_IPC_DECODE completed but with error (%d) (size=%d, data=%p)\n",
			pctx->name,
			ctx->cb_err, param->size, param->data);
		pctx->sys_errors++;
		return -EIO;
	}

	return 0;
};

void delta_ipc_close(void *hdl)
{
	struct delta_ipc_ctx *ctx = to_ctx(hdl);
	struct delta_ctx *pctx = to_pctx(ctx);
	struct delta_dev *delta = pctx->dev;
	struct rpmsg_device *rpmsg_device = delta->rpmsg_device;
	struct delta_ipc_close_msg msg;
	int ret;

	if (!hdl) {
		dev_err(delta->dev,
			"%s   ipc: failed to close, invalid ipc handle\n",
			pctx->name);
		return;
	}

	if (ctx->ipc_buf) {
		hw_free(pctx, ctx->ipc_buf);
		ctx->ipc_buf = NULL;
	}

	if (!rpmsg_device) {
		dev_err(delta->dev,
			"%s   ipc: failed to close, rpmsg is not initialized\n",
			pctx->name);
		return;
	}

	/* build rpmsg message */
	build_msg_header(ctx, DELTA_IPC_CLOSE, &msg.header);

	/* send it */
	ret = rpmsg_send(rpmsg_device->ept, &msg, sizeof(msg));
	if (ret) {
		dev_err(delta->dev,
			"%s   ipc: failed to close, rpmsg_send failed (%d) for DELTA_IPC_CLOSE\n",
			pctx->name, ret);
		pctx->sys_errors++;
		return;
	}

	/* wait for acknowledge */
	if (!wait_for_completion_timeout
	    (&ctx->done, msecs_to_jiffies(IPC_TIMEOUT))) {
		dev_err(delta->dev,
			"%s   ipc: failed to close, timeout waiting for DELTA_IPC_CLOSE callback\n",
			pctx->name);
		pctx->sys_errors++;
		return;
	}

	/* command completed, check status */
	if (ctx->cb_err) {
		dev_err(delta->dev,
			"%s   ipc: failed to close, DELTA_IPC_CLOSE completed but with error (%d)\n",
			pctx->name, ctx->cb_err);
		pctx->sys_errors++;
	}
};

static int delta_ipc_cb(struct rpmsg_device *rpdev, void *data,
			int len, void *priv, u32 src)
{
	struct delta_ipc_ctx *ctx;
	struct delta_ipc_cb_msg *msg;

	/* sanity check */
	if (!rpdev) {
		dev_err(NULL, "rpdev is NULL\n");
		return -EINVAL;
	}

	if (!data || !len) {
		dev_err(&rpdev->dev,
			"unexpected empty message received from src=%d\n", src);
		return -EINVAL;
	}

	if (len != sizeof(*msg)) {
		dev_err(&rpdev->dev,
			"unexpected message length received from src=%d (received %d bytes while %zu bytes expected)\n",
			len, src, sizeof(*msg));
		return -EINVAL;
	}

	msg = (struct delta_ipc_cb_msg *)data;
	if (msg->header.tag != IPC_SANITY_TAG) {
		dev_err(&rpdev->dev,
			"unexpected message tag received from src=%d (received %x tag while %x expected)\n",
			src, msg->header.tag, IPC_SANITY_TAG);
		return -EINVAL;
	}

	ctx = msg_to_ctx(msg);
	if (!ctx) {
		dev_err(&rpdev->dev,
			"unexpected message with NULL host_hdl received from src=%d\n",
			src);
		return -EINVAL;
	}

	/*
	 * if not already known, save copro instance context
	 * to ensure re-entrance on copro side
	 */
	if (!ctx->copro_hdl)
		ctx->copro_hdl = msg_to_copro_hdl(msg);

	/*
	 * all is fine,
	 * update status & complete command
	 */
	ctx->cb_err = msg->err;
	complete(&ctx->done);

	return 0;
}

static int delta_ipc_probe(struct rpmsg_device *rpmsg_device)
{
	struct rpmsg_driver *rpdrv = to_rpmsg_driver(rpmsg_device->dev.driver);
	struct delta_dev *delta = to_delta(rpdrv);

	delta->rpmsg_device = rpmsg_device;

	return 0;
}

static void delta_ipc_remove(struct rpmsg_device *rpmsg_device)
{
	struct rpmsg_driver *rpdrv = to_rpmsg_driver(rpmsg_device->dev.driver);
	struct delta_dev *delta = to_delta(rpdrv);

	delta->rpmsg_device = NULL;
}

static struct rpmsg_device_id delta_ipc_device_id_table[] = {
	{.name = "rpmsg-delta"},
	{},
};

static struct rpmsg_driver delta_rpmsg_driver = {
	.drv = {.name = KBUILD_MODNAME},
	.id_table = delta_ipc_device_id_table,
	.probe = delta_ipc_probe,
	.callback = delta_ipc_cb,
	.remove = delta_ipc_remove,
};

int delta_ipc_init(struct delta_dev *delta)
{
	delta->rpmsg_driver = delta_rpmsg_driver;

	return register_rpmsg_driver(&delta->rpmsg_driver);
}

void delta_ipc_exit(struct delta_dev *delta)
{
	unregister_rpmsg_driver(&delta->rpmsg_driver);
}

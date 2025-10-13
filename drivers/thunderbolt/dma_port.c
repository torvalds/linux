// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt DMA configuration based mailbox support
 *
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/delay.h>
#include <linux/slab.h>

#include "dma_port.h"
#include "tb_regs.h"

#define DMA_PORT_CAP			0x3e

#define MAIL_DATA			1
#define MAIL_DATA_DWORDS		16

#define MAIL_IN				17
#define MAIL_IN_CMD_SHIFT		28
#define MAIL_IN_CMD_MASK		GENMASK(31, 28)
#define MAIL_IN_CMD_FLASH_WRITE		0x0
#define MAIL_IN_CMD_FLASH_UPDATE_AUTH	0x1
#define MAIL_IN_CMD_FLASH_READ		0x2
#define MAIL_IN_CMD_POWER_CYCLE		0x4
#define MAIL_IN_DWORDS_SHIFT		24
#define MAIL_IN_DWORDS_MASK		GENMASK(27, 24)
#define MAIL_IN_ADDRESS_SHIFT		2
#define MAIL_IN_ADDRESS_MASK		GENMASK(23, 2)
#define MAIL_IN_CSS			BIT(1)
#define MAIL_IN_OP_REQUEST		BIT(0)

#define MAIL_OUT			18
#define MAIL_OUT_STATUS_RESPONSE	BIT(29)
#define MAIL_OUT_STATUS_CMD_SHIFT	4
#define MAIL_OUT_STATUS_CMD_MASK	GENMASK(7, 4)
#define MAIL_OUT_STATUS_MASK		GENMASK(3, 0)
#define MAIL_OUT_STATUS_COMPLETED	0
#define MAIL_OUT_STATUS_ERR_AUTH	1
#define MAIL_OUT_STATUS_ERR_ACCESS	2

#define DMA_PORT_TIMEOUT		5000 /* ms */
#define DMA_PORT_RETRIES		3

/**
 * struct tb_dma_port - DMA control port
 * @sw: Switch the DMA port belongs to
 * @port: Switch port number where DMA capability is found
 * @base: Start offset of the mailbox registers
 * @buf: Temporary buffer to store a single block
 */
struct tb_dma_port {
	struct tb_switch *sw;
	u8 port;
	u32 base;
	u8 *buf;
};

/*
 * When the switch is in safe mode it supports very little functionality
 * so we don't validate that much here.
 */
static bool dma_port_match(const struct tb_cfg_request *req,
			   const struct ctl_pkg *pkg)
{
	u64 route = tb_cfg_get_route(pkg->buffer) & ~BIT_ULL(63);

	if (pkg->frame.eof == TB_CFG_PKG_ERROR)
		return true;
	if (pkg->frame.eof != req->response_type)
		return false;
	if (route != tb_cfg_get_route(req->request))
		return false;
	if (pkg->frame.size != req->response_size)
		return false;

	return true;
}

static bool dma_port_copy(struct tb_cfg_request *req, const struct ctl_pkg *pkg)
{
	memcpy(req->response, pkg->buffer, req->response_size);
	return true;
}

static int dma_port_read(struct tb_ctl *ctl, void *buffer, u64 route,
			 u32 port, u32 offset, u32 length, int timeout_msec)
{
	struct cfg_read_pkg request = {
		.header = tb_cfg_make_header(route),
		.addr = {
			.seq = 1,
			.port = port,
			.space = TB_CFG_PORT,
			.offset = offset,
			.length = length,
		},
	};
	struct tb_cfg_request *req;
	struct cfg_write_pkg reply;
	struct tb_cfg_result res;

	req = tb_cfg_request_alloc();
	if (!req)
		return -ENOMEM;

	req->match = dma_port_match;
	req->copy = dma_port_copy;
	req->request = &request;
	req->request_size = sizeof(request);
	req->request_type = TB_CFG_PKG_READ;
	req->response = &reply;
	req->response_size = 12 + 4 * length;
	req->response_type = TB_CFG_PKG_READ;

	res = tb_cfg_request_sync(ctl, req, timeout_msec);

	tb_cfg_request_put(req);

	if (res.err)
		return res.err;

	memcpy(buffer, &reply.data, 4 * length);
	return 0;
}

static int dma_port_write(struct tb_ctl *ctl, const void *buffer, u64 route,
			  u32 port, u32 offset, u32 length, int timeout_msec)
{
	struct cfg_write_pkg request = {
		.header = tb_cfg_make_header(route),
		.addr = {
			.seq = 1,
			.port = port,
			.space = TB_CFG_PORT,
			.offset = offset,
			.length = length,
		},
	};
	struct tb_cfg_request *req;
	struct cfg_read_pkg reply;
	struct tb_cfg_result res;

	memcpy(&request.data, buffer, length * 4);

	req = tb_cfg_request_alloc();
	if (!req)
		return -ENOMEM;

	req->match = dma_port_match;
	req->copy = dma_port_copy;
	req->request = &request;
	req->request_size = 12 + 4 * length;
	req->request_type = TB_CFG_PKG_WRITE;
	req->response = &reply;
	req->response_size = sizeof(reply);
	req->response_type = TB_CFG_PKG_WRITE;

	res = tb_cfg_request_sync(ctl, req, timeout_msec);

	tb_cfg_request_put(req);

	return res.err;
}

static int dma_find_port(struct tb_switch *sw)
{
	static const int ports[] = { 3, 5, 7 };
	int i;

	/*
	 * The DMA (NHI) port is either 3, 5 or 7 depending on the
	 * controller. Try all of them.
	 */
	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		u32 type;
		int ret;

		ret = dma_port_read(sw->tb->ctl, &type, tb_route(sw), ports[i],
				    2, 1, DMA_PORT_TIMEOUT);
		if (!ret && (type & 0xffffff) == TB_TYPE_NHI)
			return ports[i];
	}

	return -ENODEV;
}

/**
 * dma_port_alloc() - Finds DMA control port from a switch pointed by route
 * @sw: Switch from where find the DMA port
 *
 * Function checks if the switch NHI port supports DMA configuration
 * based mailbox capability and if it does, allocates and initializes
 * DMA port structure. Returns %NULL if the capabity was not found.
 *
 * The DMA control port is functional also when the switch is in safe
 * mode.
 *
 * Return: &struct tb_dma_port on success, %NULL otherwise.
 */
struct tb_dma_port *dma_port_alloc(struct tb_switch *sw)
{
	struct tb_dma_port *dma;
	int port;

	port = dma_find_port(sw);
	if (port < 0)
		return NULL;

	dma = kzalloc(sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return NULL;

	dma->buf = kmalloc_array(MAIL_DATA_DWORDS, sizeof(u32), GFP_KERNEL);
	if (!dma->buf) {
		kfree(dma);
		return NULL;
	}

	dma->sw = sw;
	dma->port = port;
	dma->base = DMA_PORT_CAP;

	return dma;
}

/**
 * dma_port_free() - Release DMA control port structure
 * @dma: DMA control port
 */
void dma_port_free(struct tb_dma_port *dma)
{
	if (dma) {
		kfree(dma->buf);
		kfree(dma);
	}
}

static int dma_port_wait_for_completion(struct tb_dma_port *dma,
					unsigned int timeout)
{
	unsigned long end = jiffies + msecs_to_jiffies(timeout);
	struct tb_switch *sw = dma->sw;

	do {
		int ret;
		u32 in;

		ret = dma_port_read(sw->tb->ctl, &in, tb_route(sw), dma->port,
				    dma->base + MAIL_IN, 1, 50);
		if (ret) {
			if (ret != -ETIMEDOUT)
				return ret;
		} else if (!(in & MAIL_IN_OP_REQUEST)) {
			return 0;
		}

		usleep_range(50, 100);
	} while (time_before(jiffies, end));

	return -ETIMEDOUT;
}

static int status_to_errno(u32 status)
{
	switch (status & MAIL_OUT_STATUS_MASK) {
	case MAIL_OUT_STATUS_COMPLETED:
		return 0;
	case MAIL_OUT_STATUS_ERR_AUTH:
		return -EINVAL;
	case MAIL_OUT_STATUS_ERR_ACCESS:
		return -EACCES;
	}

	return -EIO;
}

static int dma_port_request(struct tb_dma_port *dma, u32 in,
			    unsigned int timeout)
{
	struct tb_switch *sw = dma->sw;
	u32 out;
	int ret;

	ret = dma_port_write(sw->tb->ctl, &in, tb_route(sw), dma->port,
			     dma->base + MAIL_IN, 1, DMA_PORT_TIMEOUT);
	if (ret)
		return ret;

	ret = dma_port_wait_for_completion(dma, timeout);
	if (ret)
		return ret;

	ret = dma_port_read(sw->tb->ctl, &out, tb_route(sw), dma->port,
			    dma->base + MAIL_OUT, 1, DMA_PORT_TIMEOUT);
	if (ret)
		return ret;

	return status_to_errno(out);
}

static int dma_port_flash_read_block(void *data, unsigned int dwaddress,
				     void *buf, size_t dwords)
{
	struct tb_dma_port *dma = data;
	struct tb_switch *sw = dma->sw;
	int ret;
	u32 in;

	in = MAIL_IN_CMD_FLASH_READ << MAIL_IN_CMD_SHIFT;
	if (dwords < MAIL_DATA_DWORDS)
		in |= (dwords << MAIL_IN_DWORDS_SHIFT) & MAIL_IN_DWORDS_MASK;
	in |= (dwaddress << MAIL_IN_ADDRESS_SHIFT) & MAIL_IN_ADDRESS_MASK;
	in |= MAIL_IN_OP_REQUEST;

	ret = dma_port_request(dma, in, DMA_PORT_TIMEOUT);
	if (ret)
		return ret;

	return dma_port_read(sw->tb->ctl, buf, tb_route(sw), dma->port,
			     dma->base + MAIL_DATA, dwords, DMA_PORT_TIMEOUT);
}

static int dma_port_flash_write_block(void *data, unsigned int dwaddress,
				      const void *buf, size_t dwords)
{
	struct tb_dma_port *dma = data;
	struct tb_switch *sw = dma->sw;
	int ret;
	u32 in;

	/* Write the block to MAIL_DATA registers */
	ret = dma_port_write(sw->tb->ctl, buf, tb_route(sw), dma->port,
			    dma->base + MAIL_DATA, dwords, DMA_PORT_TIMEOUT);
	if (ret)
		return ret;

	in = MAIL_IN_CMD_FLASH_WRITE << MAIL_IN_CMD_SHIFT;

	/* CSS header write is always done to the same magic address */
	if (dwaddress >= DMA_PORT_CSS_ADDRESS)
		in |= MAIL_IN_CSS;

	in |= ((dwords - 1) << MAIL_IN_DWORDS_SHIFT) & MAIL_IN_DWORDS_MASK;
	in |= (dwaddress << MAIL_IN_ADDRESS_SHIFT) & MAIL_IN_ADDRESS_MASK;
	in |= MAIL_IN_OP_REQUEST;

	return dma_port_request(dma, in, DMA_PORT_TIMEOUT);
}

/**
 * dma_port_flash_read() - Read from active flash region
 * @dma: DMA control port
 * @address: Address relative to the start of active region
 * @buf: Buffer where the data is read
 * @size: Size of the buffer
 *
 * Return: %0 on success, negative errno otherwise.
 */
int dma_port_flash_read(struct tb_dma_port *dma, unsigned int address,
			void *buf, size_t size)
{
	return tb_nvm_read_data(address, buf, size, DMA_PORT_RETRIES,
				dma_port_flash_read_block, dma);
}

/**
 * dma_port_flash_write() - Write to non-active flash region
 * @dma: DMA control port
 * @address: Address relative to the start of non-active region
 * @buf: Data to write
 * @size: Size of the buffer
 *
 * Writes block of data to the non-active flash region of the switch. If
 * the address is given as %DMA_PORT_CSS_ADDRESS the block is written
 * using CSS command.
 *
 * Return: %0 on success, negative errno otherwise.
 */
int dma_port_flash_write(struct tb_dma_port *dma, unsigned int address,
			 const void *buf, size_t size)
{
	if (address >= DMA_PORT_CSS_ADDRESS && size > DMA_PORT_CSS_MAX_SIZE)
		return -E2BIG;

	return tb_nvm_write_data(address, buf, size, DMA_PORT_RETRIES,
				 dma_port_flash_write_block, dma);
}

/**
 * dma_port_flash_update_auth() - Starts flash authenticate cycle
 * @dma: DMA control port
 *
 * Starts the flash update authentication cycle. If the image in the
 * non-active area was valid, the switch starts upgrade process where
 * active and non-active area get swapped in the end. Caller should call
 * dma_port_flash_update_auth_status() to get status of this command.
 * This is because if the switch in question is root switch the
 * thunderbolt host controller gets reset as well.
 *
 * Return: %0 on success, negative errno otherwise.
 */
int dma_port_flash_update_auth(struct tb_dma_port *dma)
{
	u32 in;

	in = MAIL_IN_CMD_FLASH_UPDATE_AUTH << MAIL_IN_CMD_SHIFT;
	in |= MAIL_IN_OP_REQUEST;

	return dma_port_request(dma, in, 150);
}

/**
 * dma_port_flash_update_auth_status() - Reads status of update auth command
 * @dma: DMA control port
 * @status: Status code of the operation
 *
 * The function checks if there is status available from the last update
 * auth command.
 *
 * Return:
 * * %0 - If there is no status and no further action is required.
 * * %1 - If there is some status. @status holds the failure code.
 * * Negative errno - An error occurred when reading status from the
 *   switch.
 */
int dma_port_flash_update_auth_status(struct tb_dma_port *dma, u32 *status)
{
	struct tb_switch *sw = dma->sw;
	u32 out, cmd;
	int ret;

	ret = dma_port_read(sw->tb->ctl, &out, tb_route(sw), dma->port,
			    dma->base + MAIL_OUT, 1, DMA_PORT_TIMEOUT);
	if (ret)
		return ret;

	/* Check if the status relates to flash update auth */
	cmd = (out & MAIL_OUT_STATUS_CMD_MASK) >> MAIL_OUT_STATUS_CMD_SHIFT;
	if (cmd == MAIL_IN_CMD_FLASH_UPDATE_AUTH) {
		if (status)
			*status = out & MAIL_OUT_STATUS_MASK;

		/* Reset is needed in any case */
		return 1;
	}

	return 0;
}

/**
 * dma_port_power_cycle() - Power cycles the switch
 * @dma: DMA control port
 *
 * Triggers power cycle to the switch.
 *
 * Return: %0 on success, negative errno otherwise.
 */
int dma_port_power_cycle(struct tb_dma_port *dma)
{
	u32 in;

	in = MAIL_IN_CMD_POWER_CYCLE << MAIL_IN_CMD_SHIFT;
	in |= MAIL_IN_OP_REQUEST;

	return dma_port_request(dma, in, 150);
}

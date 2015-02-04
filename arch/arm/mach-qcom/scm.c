/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/err.h>

#include <asm/outercache.h>
#include <asm/cacheflush.h>

#include "scm.h"

#define SCM_ENOMEM		-5
#define SCM_EOPNOTSUPP		-4
#define SCM_EINVAL_ADDR		-3
#define SCM_EINVAL_ARG		-2
#define SCM_ERROR		-1
#define SCM_INTERRUPTED		1

static DEFINE_MUTEX(scm_lock);

/**
 * struct scm_command - one SCM command buffer
 * @len: total available memory for command and response
 * @buf_offset: start of command buffer
 * @resp_hdr_offset: start of response buffer
 * @id: command to be executed
 * @buf: buffer returned from scm_get_command_buffer()
 *
 * An SCM command is laid out in memory as follows:
 *
 *	------------------- <--- struct scm_command
 *	| command header  |
 *	------------------- <--- scm_get_command_buffer()
 *	| command buffer  |
 *	------------------- <--- struct scm_response and
 *	| response header |      scm_command_to_response()
 *	------------------- <--- scm_get_response_buffer()
 *	| response buffer |
 *	-------------------
 *
 * There can be arbitrary padding between the headers and buffers so
 * you should always use the appropriate scm_get_*_buffer() routines
 * to access the buffers in a safe manner.
 */
struct scm_command {
	__le32 len;
	__le32 buf_offset;
	__le32 resp_hdr_offset;
	__le32 id;
	__le32 buf[0];
};

/**
 * struct scm_response - one SCM response buffer
 * @len: total available memory for response
 * @buf_offset: start of response data relative to start of scm_response
 * @is_complete: indicates if the command has finished processing
 */
struct scm_response {
	__le32 len;
	__le32 buf_offset;
	__le32 is_complete;
};

/**
 * alloc_scm_command() - Allocate an SCM command
 * @cmd_size: size of the command buffer
 * @resp_size: size of the response buffer
 *
 * Allocate an SCM command, including enough room for the command
 * and response headers as well as the command and response buffers.
 *
 * Returns a valid &scm_command on success or %NULL if the allocation fails.
 */
static struct scm_command *alloc_scm_command(size_t cmd_size, size_t resp_size)
{
	struct scm_command *cmd;
	size_t len = sizeof(*cmd) + sizeof(struct scm_response) + cmd_size +
		resp_size;
	u32 offset;

	cmd = kzalloc(PAGE_ALIGN(len), GFP_KERNEL);
	if (cmd) {
		cmd->len = cpu_to_le32(len);
		offset = offsetof(struct scm_command, buf);
		cmd->buf_offset = cpu_to_le32(offset);
		cmd->resp_hdr_offset = cpu_to_le32(offset + cmd_size);
	}
	return cmd;
}

/**
 * free_scm_command() - Free an SCM command
 * @cmd: command to free
 *
 * Free an SCM command.
 */
static inline void free_scm_command(struct scm_command *cmd)
{
	kfree(cmd);
}

/**
 * scm_command_to_response() - Get a pointer to a scm_response
 * @cmd: command
 *
 * Returns a pointer to a response for a command.
 */
static inline struct scm_response *scm_command_to_response(
		const struct scm_command *cmd)
{
	return (void *)cmd + le32_to_cpu(cmd->resp_hdr_offset);
}

/**
 * scm_get_command_buffer() - Get a pointer to a command buffer
 * @cmd: command
 *
 * Returns a pointer to the command buffer of a command.
 */
static inline void *scm_get_command_buffer(const struct scm_command *cmd)
{
	return (void *)cmd->buf;
}

/**
 * scm_get_response_buffer() - Get a pointer to a response buffer
 * @rsp: response
 *
 * Returns a pointer to a response buffer of a response.
 */
static inline void *scm_get_response_buffer(const struct scm_response *rsp)
{
	return (void *)rsp + le32_to_cpu(rsp->buf_offset);
}

static int scm_remap_error(int err)
{
	pr_err("scm_call failed with error code %d\n", err);
	switch (err) {
	case SCM_ERROR:
		return -EIO;
	case SCM_EINVAL_ADDR:
	case SCM_EINVAL_ARG:
		return -EINVAL;
	case SCM_EOPNOTSUPP:
		return -EOPNOTSUPP;
	case SCM_ENOMEM:
		return -ENOMEM;
	}
	return -EINVAL;
}

static u32 smc(u32 cmd_addr)
{
	int context_id;
	register u32 r0 asm("r0") = 1;
	register u32 r1 asm("r1") = (u32)&context_id;
	register u32 r2 asm("r2") = cmd_addr;
	do {
		asm volatile(
			__asmeq("%0", "r0")
			__asmeq("%1", "r0")
			__asmeq("%2", "r1")
			__asmeq("%3", "r2")
#ifdef REQUIRES_SEC
			".arch_extension sec\n"
#endif
			"smc	#0	@ switch to secure world\n"
			: "=r" (r0)
			: "r" (r0), "r" (r1), "r" (r2)
			: "r3");
	} while (r0 == SCM_INTERRUPTED);

	return r0;
}

static int __scm_call(const struct scm_command *cmd)
{
	int ret;
	u32 cmd_addr = virt_to_phys(cmd);

	/*
	 * Flush the command buffer so that the secure world sees
	 * the correct data.
	 */
	__cpuc_flush_dcache_area((void *)cmd, cmd->len);
	outer_flush_range(cmd_addr, cmd_addr + cmd->len);

	ret = smc(cmd_addr);
	if (ret < 0)
		ret = scm_remap_error(ret);

	return ret;
}

static void scm_inv_range(unsigned long start, unsigned long end)
{
	u32 cacheline_size, ctr;

	asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r" (ctr));
	cacheline_size = 4 << ((ctr >> 16) & 0xf);

	start = round_down(start, cacheline_size);
	end = round_up(end, cacheline_size);
	outer_inv_range(start, end);
	while (start < end) {
		asm ("mcr p15, 0, %0, c7, c6, 1" : : "r" (start)
		     : "memory");
		start += cacheline_size;
	}
	dsb();
	isb();
}

/**
 * scm_call() - Send an SCM command
 * @svc_id: service identifier
 * @cmd_id: command identifier
 * @cmd_buf: command buffer
 * @cmd_len: length of the command buffer
 * @resp_buf: response buffer
 * @resp_len: length of the response buffer
 *
 * Sends a command to the SCM and waits for the command to finish processing.
 *
 * A note on cache maintenance:
 * Note that any buffers that are expected to be accessed by the secure world
 * must be flushed before invoking scm_call and invalidated in the cache
 * immediately after scm_call returns. Cache maintenance on the command and
 * response buffers is taken care of by scm_call; however, callers are
 * responsible for any other cached buffers passed over to the secure world.
 */
static int scm_call(u32 svc_id, u32 cmd_id, const void *cmd_buf, size_t cmd_len,
			void *resp_buf, size_t resp_len)
{
	int ret;
	struct scm_command *cmd;
	struct scm_response *rsp;
	unsigned long start, end;

	cmd = alloc_scm_command(cmd_len, resp_len);
	if (!cmd)
		return -ENOMEM;

	cmd->id = cpu_to_le32((svc_id << 10) | cmd_id);
	if (cmd_buf)
		memcpy(scm_get_command_buffer(cmd), cmd_buf, cmd_len);

	mutex_lock(&scm_lock);
	ret = __scm_call(cmd);
	mutex_unlock(&scm_lock);
	if (ret)
		goto out;

	rsp = scm_command_to_response(cmd);
	start = (unsigned long)rsp;

	do {
		scm_inv_range(start, start + sizeof(*rsp));
	} while (!rsp->is_complete);

	end = (unsigned long)scm_get_response_buffer(rsp) + resp_len;
	scm_inv_range(start, end);

	if (resp_buf)
		memcpy(resp_buf, scm_get_response_buffer(rsp), resp_len);
out:
	free_scm_command(cmd);
	return ret;
}

u32 scm_get_version(void)
{
	int context_id;
	static u32 version = -1;
	register u32 r0 asm("r0");
	register u32 r1 asm("r1");

	if (version != -1)
		return version;

	mutex_lock(&scm_lock);

	r0 = 0x1 << 8;
	r1 = (u32)&context_id;
	do {
		asm volatile(
			__asmeq("%0", "r0")
			__asmeq("%1", "r1")
			__asmeq("%2", "r0")
			__asmeq("%3", "r1")
#ifdef REQUIRES_SEC
			".arch_extension sec\n"
#endif
			"smc	#0	@ switch to secure world\n"
			: "=r" (r0), "=r" (r1)
			: "r" (r0), "r" (r1)
			: "r2", "r3");
	} while (r0 == SCM_INTERRUPTED);

	version = r1;
	mutex_unlock(&scm_lock);

	return version;
}
EXPORT_SYMBOL(scm_get_version);

#define SCM_SVC_BOOT			0x1
#define SCM_BOOT_ADDR			0x1
/*
 * Set the cold/warm boot address for one of the CPU cores.
 */
int scm_set_boot_addr(u32 addr, int flags)
{
	struct {
		__le32 flags;
		__le32 addr;
	} cmd;

	cmd.addr = cpu_to_le32(addr);
	cmd.flags = cpu_to_le32(flags);
	return scm_call(SCM_SVC_BOOT, SCM_BOOT_ADDR,
			&cmd, sizeof(cmd), NULL, 0);
}
EXPORT_SYMBOL(scm_set_boot_addr);

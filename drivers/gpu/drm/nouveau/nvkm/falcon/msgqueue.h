/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NVKM_CORE_FALCON_MSGQUEUE_H
#define __NVKM_CORE_FALCON_MSGQUEUE_H
#include <core/falcon.h>
#include <core/msgqueue.h>

/*
 * The struct nvkm_msgqueue (named so for lack of better candidate) manages
 * a firmware (typically, NVIDIA signed firmware) running under a given falcon.
 *
 * Such firmwares expect to receive commands (through one or several command
 * queues) and will reply to such command by sending messages (using one
 * message queue).
 *
 * Each firmware can support one or several units - ACR for managing secure
 * falcons, PMU for power management, etc. A unit can be seen as a class to
 * which command can be sent.
 *
 * One usage example would be to send a command to the SEC falcon to ask it to
 * reset a secure falcon. The SEC falcon will receive the command, process it,
 * and send a message to signal success or failure. Only when the corresponding
 * message is received can the requester assume the request has been processed.
 *
 * Since we expect many variations between the firmwares NVIDIA will release
 * across GPU generations, this library is built in a very modular way. Message
 * formats and queues details (such as number of usage) are left to
 * specializations of struct nvkm_msgqueue, while the functions in msgqueue.c
 * take care of posting commands and processing messages in a fashion that is
 * universal.
 *
 */

/**
 * struct nvkm_msgqueue_hdr - header for all commands/messages
 * @unit_id:	id of firmware using receiving the command/sending the message
 * @size:	total size of command/message
 * @ctrl_flags:	type of command/message
 * @seq_id:	used to match a message from its corresponding command
 */
struct nvkm_msgqueue_hdr {
	u8 unit_id;
	u8 size;
	u8 ctrl_flags;
	u8 seq_id;
};

/**
 * struct nvkm_msgqueue_msg - base message.
 *
 * This is just a header and a message (or command) type. Useful when
 * building command-specific structures.
 */
struct nvkm_msgqueue_msg {
	struct nvkm_msgqueue_hdr hdr;
	u8 msg_type;
};

struct nvkm_msgqueue;

/**
 * struct nvkm_msgqueue_init_func - msgqueue functions related to initialization
 *
 * @gen_cmdline:	build the commandline into a pre-allocated buffer
 * @init_callback:	called to process the init message
 */
struct nvkm_msgqueue_init_func {
	void (*gen_cmdline)(struct nvkm_msgqueue *, void *);
	int (*init_callback)(struct nvkm_msgqueue *, struct nvkm_msgqueue_hdr *);
};

/**
 * struct nvkm_msgqueue_acr_func - msgqueue functions related to ACR
 *
 * @boot_falcon:	build and send the command to reset a given falcon
 * @boot_multiple_falcons: build and send the command to reset several falcons
 */
struct nvkm_msgqueue_acr_func {
	int (*boot_falcon)(struct nvkm_msgqueue *, enum nvkm_secboot_falcon);
	int (*boot_multiple_falcons)(struct nvkm_msgqueue *, unsigned long);
};

struct nvkm_msgqueue_func {
	const struct nvkm_msgqueue_init_func *init_func;
	const struct nvkm_msgqueue_acr_func *acr_func;
	void (*dtor)(struct nvkm_msgqueue *);
	void (*recv)(struct nvkm_msgqueue *queue);
};

/**
 * struct nvkm_msgqueue_queue - information about a command or message queue
 *
 * The number of queues is firmware-dependent. All queues must have their
 * information filled by the init message handler.
 *
 * @mutex_lock:	to be acquired when the queue is being used
 * @index:	physical queue index
 * @offset:	DMEM offset where this queue begins
 * @size:	size allocated to this queue in DMEM (in bytes)
 * @position:	current write position
 * @head_reg:	address of the HEAD register for this queue
 * @tail_reg:	address of the TAIL register for this queue
 */
struct nvkm_msgqueue_queue {
	struct nvkm_falcon_qmgr *qmgr;
	const char *name;
	struct mutex mutex;
	u32 index;
	u32 offset;
	u32 size;
	u32 position;

	u32 head_reg;
	u32 tail_reg;

	struct completion ready;
};

/**
 * struct nvkm_msgqueue - manage a command/message based FW on a falcon
 *
 * @falcon:	falcon to be managed
 * @func:	implementation of the firmware to use
 * @init_msg_received:	whether the init message has already been received
  */
struct nvkm_msgqueue {
	struct nvkm_falcon *falcon;
	const struct nvkm_msgqueue_func *func;
	u32 fw_version;
	bool init_msg_received;
};

void nvkm_msgqueue_ctor(const struct nvkm_msgqueue_func *, struct nvkm_falcon *,
			struct nvkm_msgqueue *);
void nvkm_msgqueue_process_msgs(struct nvkm_msgqueue *,
				struct nvkm_msgqueue_queue *);

int msgqueue_0137c63d_new(struct nvkm_falcon *, const struct nvkm_secboot *,
			  struct nvkm_msgqueue **);
int msgqueue_0137bca5_new(struct nvkm_falcon *, const struct nvkm_secboot *,
			  struct nvkm_msgqueue **);
int msgqueue_0148cdec_new(struct nvkm_falcon *, const struct nvkm_secboot *,
			  struct nvkm_msgqueue **);

#endif

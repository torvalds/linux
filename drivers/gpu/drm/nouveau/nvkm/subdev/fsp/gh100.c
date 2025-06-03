/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/drf.h>
#include <nvhw/ref/gh100/dev_fsp_pri.h>
#include <nvhw/ref/gh100/dev_therm.h>

#include <nvrm/nvtypes.h>

#define MCTP_HEADER_VERSION          3:0
#define MCTP_HEADER_RSVD             7:4

#define MCTP_HEADER_DEID            15:8
#define MCTP_HEADER_SEID            23:16

#define MCTP_HEADER_TAG             26:24
#define MCTP_HEADER_TO              27:27
#define MCTP_HEADER_SEQ             29:28
#define MCTP_HEADER_EOM             30:30
#define MCTP_HEADER_SOM             31:31

#define MCTP_MSG_HEADER_TYPE         6:0
#define MCTP_MSG_HEADER_IC           7:7

#define MCTP_MSG_HEADER_VENDOR_ID   23:8
#define MCTP_MSG_HEADER_NVDM_TYPE   31:24

#define MCTP_MSG_HEADER_TYPE_VENDOR_PCI 0x7e
#define MCTP_MSG_HEADER_VENDOR_ID_NV    0x10de

#define NVDM_TYPE_COT                   0x14
#define NVDM_TYPE_FSP_RESPONSE          0x15

#pragma pack(1)
typedef struct nvdm_payload_cot
{
    NvU16 version;
    NvU16 size;
    NvU64 gspFmcSysmemOffset;
    NvU64 frtsSysmemOffset;
    NvU32 frtsSysmemSize;

    // Note this is an offset from the end of FB
    NvU64 frtsVidmemOffset;
    NvU32 frtsVidmemSize;

    // Authentication related fields
    NvU32 hash384[12];
    NvU32 publicKey[96];
    NvU32 signature[96];

    NvU64 gspBootArgsSysmemOffset;
} NVDM_PAYLOAD_COT;
#pragma pack()

#pragma pack(1)
typedef struct
{
    NvU32 taskId;
    NvU32 commandNvdmType;
    NvU32 errorCode;
} NVDM_PAYLOAD_COMMAND_RESPONSE;
#pragma pack()

static u32
gh100_fsp_poll(struct nvkm_fsp *fsp)
{
	struct nvkm_device *device = fsp->subdev.device;
	u32 head, tail;

	head = nvkm_rd32(device, NV_PFSP_MSGQ_HEAD(0));
	tail = nvkm_rd32(device, NV_PFSP_MSGQ_TAIL(0));

	if (head == tail)
		return 0;

	return (tail - head) + sizeof(u32); /* TAIL points at last DWORD written. */
}

static int
gh100_fsp_recv(struct nvkm_fsp *fsp, u8 *packet, u32 max_packet_size)
{
	struct nvkm_device *device = fsp->subdev.device;
	u32 packet_size;
	int ret;

	packet_size = gh100_fsp_poll(fsp);
	if (!packet_size || WARN_ON(packet_size % 4 || packet_size > max_packet_size))
		return -EINVAL;

	ret = nvkm_falcon_pio_rd(&fsp->falcon, 0, EMEM, 0, packet, 0, packet_size);
	if (ret)
		return ret;

	nvkm_wr32(device, NV_PFSP_MSGQ_TAIL(0), 0);
	nvkm_wr32(device, NV_PFSP_MSGQ_HEAD(0), 0);

	return packet_size;
}

static int
gh100_fsp_wait(struct nvkm_fsp *fsp)
{
	int time = 1000;

	do {
		if (gh100_fsp_poll(fsp))
			return 0;

		usleep_range(1000, 2000);
	} while(time--);

	return -ETIMEDOUT;
}

static int
gh100_fsp_send(struct nvkm_fsp *fsp, const u8 *packet, u32 packet_size)
{
	struct nvkm_device *device = fsp->subdev.device;
	int time = 1000, ret;

	if (WARN_ON(packet_size % sizeof(u32)))
		return -EINVAL;

	/* Ensure any previously sent message has been consumed. */
	do {
		u32 head = nvkm_rd32(device, NV_PFSP_QUEUE_HEAD(0));
		u32 tail = nvkm_rd32(device, NV_PFSP_QUEUE_TAIL(0));

		if (tail == head)
			break;

		usleep_range(1000, 2000);
	} while(time--);

	if (time < 0)
		return -ETIMEDOUT;

	/* Write message to EMEM. */
	ret = nvkm_falcon_pio_wr(&fsp->falcon, packet, 0, 0, EMEM, 0, packet_size, 0, false);
	if (ret)
		return ret;

	/* Update queue pointers - TAIL points at last DWORD written. */
	nvkm_wr32(device, NV_PFSP_QUEUE_TAIL(0), packet_size - sizeof(u32));
	nvkm_wr32(device, NV_PFSP_QUEUE_HEAD(0), 0);
	return 0;
}

static int
gh100_fsp_send_sync(struct nvkm_fsp *fsp, u8 nvdm_type, const u8 *packet, u32 packet_size)
{
	struct nvkm_subdev *subdev = &fsp->subdev;
	struct {
		u32 mctp_header;
		u32 nvdm_header;
		NVDM_PAYLOAD_COMMAND_RESPONSE response;
	} reply;
	int ret;

	ret = gh100_fsp_send(fsp, packet, packet_size);
	if (ret)
		return ret;

	ret = gh100_fsp_wait(fsp);
	if (ret)
		return ret;

	ret = gh100_fsp_recv(fsp, (u8 *)&reply, sizeof(reply));
	if (ret < 0)
		return ret;

	if (NVVAL_TEST(reply.mctp_header, MCTP, HEADER, SOM, !=, 1) ||
	    NVVAL_TEST(reply.mctp_header, MCTP, HEADER, EOM, !=, 1)) {
		nvkm_error(subdev, "unexpected MCTP header in reply: 0x%08x\n", reply.mctp_header);
		return -EIO;
	}

	if (NVDEF_TEST(reply.nvdm_header, MCTP, MSG_HEADER, TYPE, !=, VENDOR_PCI) ||
	    NVDEF_TEST(reply.nvdm_header, MCTP, MSG_HEADER, VENDOR_ID, !=, NV) ||
	    NVVAL_TEST(reply.nvdm_header, MCTP, MSG_HEADER, NVDM_TYPE, !=, NVDM_TYPE_FSP_RESPONSE)) {
		nvkm_error(subdev, "unexpected NVDM header in reply: 0x%08x\n", reply.nvdm_header);
		return -EIO;
	}

	if (reply.response.commandNvdmType != nvdm_type) {
		nvkm_error(subdev, "expected NVDM type 0x%02x in reply, got 0x%02x\n",
			   nvdm_type, reply.response.commandNvdmType);
		return -EIO;
	}

	if (reply.response.errorCode) {
		nvkm_error(subdev, "NVDM command 0x%02x failed with error 0x%08x\n",
			   nvdm_type, reply.response.errorCode);
		return -EIO;
	}

	return 0;
}

int
gh100_fsp_boot_gsp_fmc(struct nvkm_fsp *fsp, u64 args_addr, u32 rsvd_size, bool resume,
		       u64 img_addr, const u8 *hash, const u8 *pkey, const u8 *sig)
{
	struct {
		u32 mctp_header;
		u32 nvdm_header;
		NVDM_PAYLOAD_COT cot;
	} msg = {};

	msg.mctp_header = NVVAL(MCTP, HEADER, SOM, 1) |
			  NVVAL(MCTP, HEADER, EOM, 1) |
			  NVVAL(MCTP, HEADER, SEID, 0) |
			  NVVAL(MCTP, HEADER, SEQ, 0);

	msg.nvdm_header = NVDEF(MCTP, MSG_HEADER, TYPE, VENDOR_PCI) |
			  NVDEF(MCTP, MSG_HEADER, VENDOR_ID, NV) |
			  NVVAL(MCTP, MSG_HEADER, NVDM_TYPE, NVDM_TYPE_COT);

	msg.cot.version = fsp->func->cot.version;
	msg.cot.size = sizeof(msg.cot);
	msg.cot.gspFmcSysmemOffset = img_addr;
	if (!resume) {
		msg.cot.frtsVidmemOffset = ALIGN(rsvd_size, 0x200000);
		msg.cot.frtsVidmemSize = 0x100000;
	}

	memcpy(msg.cot.hash384, hash, fsp->func->cot.size_hash);
	memcpy(msg.cot.publicKey, pkey, fsp->func->cot.size_pkey);
	memcpy(msg.cot.signature, sig, fsp->func->cot.size_sig);

	msg.cot.gspBootArgsSysmemOffset = args_addr;

	return gh100_fsp_send_sync(fsp, NVDM_TYPE_COT, (const u8 *)&msg, sizeof(msg));
}

int
gh100_fsp_wait_secure_boot(struct nvkm_fsp *fsp)
{
	struct nvkm_device *device = fsp->subdev.device;
	unsigned timeout_ms = 4000;

	do {
		u32 status = NVKM_RD32(device, NV_THERM, I2CS_SCRATCH, FSP_BOOT_COMPLETE_STATUS);

		if (status == NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_SUCCESS)
			return 0;

		usleep_range(1000, 2000);
	} while (timeout_ms--);

	return -ETIMEDOUT;
}

static const struct nvkm_fsp_func
gh100_fsp = {
	.wait_secure_boot = gh100_fsp_wait_secure_boot,
	.cot = {
		.version = 1,
		.size_hash = 48,
		.size_pkey = 384,
		.size_sig = 384,
		.boot_gsp_fmc = gh100_fsp_boot_gsp_fmc,
	},
};

int
gh100_fsp_new(struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_fsp **pfsp)
{
	return nvkm_fsp_new_(&gh100_fsp, device, type, inst, pfsp);
}

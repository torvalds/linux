// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "linux/habmm.h"
#include "hgsl_hyp.h"
#include "hgsl_hyp_socket.h"
#include "hgsl_utils.h"

#define HAB_OPEN_WAIT_TIMEOUT_MS   (3000)
#define HGSL_DUMP_PAYLOAD_STR_SIZE ((HGSL_MAX_DUMP_PAYLOAD_SIZE * (2 * sizeof(uint32_t) + 3)) + 1)

int gsl_hab_open(int *habfd)
{
	int ret = 0;

	ret = habmm_socket_open(habfd
			, HAB_MMID_CREATE(MM_GFX, (int)*habfd)
			, HAB_OPEN_WAIT_TIMEOUT_MS
			, HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_SINGLE_FE);

	LOGD("habmm_socket_open returned with %d, %x", ret, *habfd);

	return ret;
}

int gsl_hab_recv(int habfd, unsigned char *p, size_t sz, int interruptible)
{
	int ret = 0;
	uint32_t size_bytes = 0;
	uint32_t flags = 0;

	if (!interruptible)
		flags |= HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE;

	size_bytes = (uint32_t)sz;
	ret = habmm_socket_recv(habfd, p, &size_bytes, 0, flags);

	if (ret && (ret != -EINTR))
		LOGE("habmm_socket_recv failed, %d, socket %x. size_bytes %u, expects %u",
				ret, habfd, size_bytes, sz);

	return ret;
}

int gsl_hab_send(int habfd, unsigned char *p, size_t sz)
{
	return habmm_socket_send(habfd, p, sz, 0);
}

int gsl_hab_close(int habfd)
{
	int ret = habmm_socket_close(habfd);

	if (ret)
		LOGE("Unable to habmm_socket_close, ret %d", ret);

	return ret;

}

enum _gsl_rpc_payload_type_t {
	GSL_RPC_BLOB_DATA = 0,
	GSL_RPC_32BIT_DATA,
	GSL_RPC_64BIT_DATA
};

#pragma pack(push, 1)
struct gsl_rpc_header_t {
	uint32_t magic;
	uint32_t id;
	uint32_t version;
	uint32_t size;
	uint8_t  data;
};

struct gsl_rpc_footer_t {
	uint32_t checksum;
};
#pragma pack(pop)

/* the actual header size is one byte less because
 * of the data pointer in the gsl_rpc_header_t
 * which is there for the ease of access to the data
 * but which is actually not being sent
 */
static const uint32_t gsl_rpc_header_size
	= sizeof(struct gsl_rpc_header_t) - 1;

uint32_t gsl_rpc_parcel_get_data_offset(void)
{
	return gsl_rpc_header_size;
}

static inline uint32_t gsl_rpc_gen_checksum(void *p_data, size_t size)
{
	/*TODO: Implement checksum generation here*/
	OS_UNUSED(p_data);
	OS_UNUSED(size);
	return (uint32_t)-1;
}

static inline int gsl_rpc_get_arg_ptr(struct gsl_hab_payload *p,
	uint32_t id, void **p_data, size_t size)
{
	int ret = -EINVAL;

	if ((p->data_pos + size + gsl_rpc_header_size) <= p->data_size) {
		struct gsl_rpc_header_t *hdr
			= (struct gsl_rpc_header_t *)(p->data + p->data_pos);

		if ((hdr->magic == GSL_HAB_DATA_MAGIC) &&
			(hdr->id == id) && (hdr->size == size)) {
			struct gsl_rpc_footer_t *footer = NULL;
			uint32_t checksum;

			checksum = gsl_rpc_gen_checksum(&hdr->data, hdr->size);
			*p_data = (void *)&hdr->data;
			p->data_pos += size + gsl_rpc_header_size;
			footer = (struct gsl_rpc_footer_t *)
				(p->data + p->data_pos);
			p->data_pos += sizeof(struct gsl_rpc_footer_t);

			if (checksum == footer->checksum)
				ret = 0;
			else
				LOGE("checksum mismatch %d != %d",
					checksum, footer->checksum);
		} else {
			struct gsl_rpc_header_t *call_hdr
				= (struct gsl_rpc_header_t *)p->data;
			size_t dump_size
				= call_hdr->size + gsl_rpc_header_size
					+ sizeof(struct gsl_rpc_footer_t);

			dump_size = (dump_size <= p->data_size) ?
						dump_size : p->data_size;
			LOGE("@%d: argument type or size mismatch: call id %d",
				p->data_pos, call_hdr->id);
			LOGE("size %d magic 0x%X/0x%X, id %d/%d, size %d/%d",
				call_hdr->size, hdr->magic, GSL_HAB_DATA_MAGIC,
				hdr->id, id, hdr->size, size);
			gsl_hab_payload_dump(p, dump_size);
		}
	}

	return ret;
}

#define GSL_RPC_READ_ARG(p, id, p_arg, type) \
({ \
	void *p_arg_data = NULL; \
	int ret = gsl_rpc_get_arg_ptr(p, id, &p_arg_data, sizeof(type)); \
	if (ret == 0) { \
		*p_arg = *((type *)p_arg_data); \
	} \
\
	ret; \
})

#define GSL_RPC_WRITE_DATA(p, type, data_ptr, len, action) \
({ \
	int status = 0; \
\
	if ((p->data_pos + gsl_rpc_header_size + len +\
		sizeof(struct gsl_rpc_footer_t) > p->data_size)) { \
		status = grow_data(p, len); \
	} \
\
	if (status == 0) { \
		struct gsl_rpc_header_t *hdr = (struct gsl_rpc_header_t *) \
			(p->data + p->data_pos); \
		struct gsl_rpc_footer_t *ftr = (struct gsl_rpc_footer_t *) \
			(p->data + p->data_pos + gsl_rpc_header_size + len); \
		void *data_ptr = (void *)&hdr->data; \
		uint32_t checksum = 0; \
\
		action; \
		checksum = gsl_rpc_gen_checksum(data_ptr, len); \
		hdr->magic = GSL_HAB_DATA_MAGIC; \
		hdr->id = type; \
		hdr->version = 2; \
		hdr->size = len; \
		ftr->checksum = checksum; \
		p->data_pos += len + gsl_rpc_header_size \
			+ sizeof(struct gsl_rpc_footer_t); \
	} \
\
	status; \
})

#define GSL_RPC_WRITE_ARG(p, id, type, val) \
	GSL_RPC_WRITE_DATA(p, id, p_data, sizeof(type), *((type *)p_data) = val)


int gsl_rpc_parcel_init(struct gsl_hab_payload *p)
{
	size_t size = 4096;

	if (p == NULL)
		return -EINVAL;

	p->data = hgsl_zalloc(size);
	if (p->data == NULL) {
		LOGE("No memory allocated\n");
		return -ENOMEM;
	}
	p->version = 1;
	p->data_size = size;
	p->data_pos = gsl_rpc_parcel_get_data_offset();
	memset(p->data, 0, size);
	return 0;
}

void gsl_rpc_parcel_free(struct gsl_hab_payload *p)
{
	if (p == NULL || p->data == NULL)
		return;

	hgsl_free(p->data);
	p->data = NULL;
}

int gsl_rpc_parcel_reset(struct gsl_hab_payload *p)
{
	int ret = -EINVAL;

	if (p == NULL || p->data == NULL) {
		LOGE("parcel isn't inited\n");
	} else {
		p->data_pos = gsl_rpc_parcel_get_data_offset();
		memset(p->data, 0, p->data_size);
		ret = 0;
	}

	return ret;
}

uint32_t gsl_rpc_parcel_get_version(struct gsl_hab_payload *p)
{
	return p->version;
}

int gsl_rpc_read(struct gsl_hab_payload *p, void *outData, size_t len)
{
	void *p_arg_data = NULL;
	int ret = gsl_rpc_get_arg_ptr(p, GSL_RPC_BLOB_DATA, &p_arg_data, len);

	if ((ret == 0) && outData && len)
		memcpy(outData, p_arg_data, len);

	return ret;
}

int gsl_rpc_read_l(struct gsl_hab_payload *p, void **pOutData, size_t len)
{
	return gsl_rpc_get_arg_ptr(p, GSL_RPC_BLOB_DATA, pOutData, len);
}

int gsl_rpc_read_int32_l(struct gsl_hab_payload *p, int32_t *pArg)
{
	return GSL_RPC_READ_ARG(p, GSL_RPC_32BIT_DATA, pArg, int32_t);
}

int gsl_rpc_read_uint32_l(struct gsl_hab_payload *p, uint32_t *pArg)
{
	return GSL_RPC_READ_ARG(p, GSL_RPC_32BIT_DATA, pArg, uint32_t);
}

int gsl_rpc_read_int64_l(struct gsl_hab_payload *p, int64_t *pArg)
{
	return GSL_RPC_READ_ARG(p, GSL_RPC_64BIT_DATA, pArg, int64_t);
}

int gsl_rpc_read_uint64_l(struct gsl_hab_payload *p, uint64_t *pArg)
{
	return GSL_RPC_READ_ARG(p, GSL_RPC_64BIT_DATA, pArg, uint64_t);
}

int gsl_rpc_finalize(struct gsl_hab_payload *p)
{
	int ret = 0;

	if ((p->data_pos + sizeof(struct gsl_rpc_footer_t)) > p->data_size)
		ret = grow_data(p, sizeof(struct gsl_rpc_footer_t));

	if (!ret) {
		struct gsl_rpc_header_t *hdr
			= (struct gsl_rpc_header_t *)p->data;
		struct gsl_rpc_footer_t *ftr
			= (struct gsl_rpc_footer_t *)(p->data + p->data_pos);
		uint32_t data_size = p->data_pos - gsl_rpc_header_size;
		uint32_t checksum = gsl_rpc_gen_checksum(&hdr->data, data_size);

		hdr->size = data_size;
		ftr->checksum = checksum;
		p->data_pos += sizeof(struct gsl_rpc_footer_t);
	}

	return ret;
}

int gsl_rpc_set_call_params(struct gsl_hab_payload *p,
	uint32_t opcode, uint32_t version)
{
	struct gsl_rpc_header_t *hdr = (struct gsl_rpc_header_t *)p->data;

	hdr->magic = GSL_HAB_CALL_MAGIC;
	hdr->id = opcode;
	hdr->version = version;

	return 0;
}

int gsl_rpc_get_call_params(struct gsl_hab_payload *p,
	uint32_t *opcode, uint32_t *version)
{
	int ret = -EINVAL;

	struct gsl_rpc_header_t *hdr = (struct gsl_rpc_header_t *)(p->data);

	if (opcode) {
		*opcode = hdr->id;
		ret = 0;
	}

	if (version) {
		*version = hdr->version;
		ret = 0;
	}

	return ret;
}

int gsl_rpc_get_data_params(struct gsl_hab_payload *p,
	void **p_data, uint32_t *size, uint32_t *max_size)
{
	int ret = -EINVAL;

	if (p_data) {
		*p_data = p->data;
		ret = 0;
	}

	if (size) {
		*size = p->data_pos;
		ret = 0;
	}

	if (max_size) {
		*max_size = p->data_size;
		ret = 0;
	}

	return ret;
}

int gsl_rpc_write(struct gsl_hab_payload *p, const void *data, size_t len)
{
	return GSL_RPC_WRITE_DATA(p, GSL_RPC_BLOB_DATA, p_data, len,
			do {if (data && len) memcpy(p_data, data, len); } while (0));
}

int gsl_rpc_write_int32(struct gsl_hab_payload *p, int32_t val)
{
	return GSL_RPC_WRITE_ARG(p, GSL_RPC_32BIT_DATA, int32_t, val);
}

int gsl_rpc_write_uint32(struct gsl_hab_payload *p, uint32_t val)
{
	return GSL_RPC_WRITE_ARG(p, GSL_RPC_32BIT_DATA, uint32_t, val);
}

int gsl_rpc_write_int64(struct gsl_hab_payload *p, int64_t val)
{
	return GSL_RPC_WRITE_ARG(p, GSL_RPC_64BIT_DATA, int64_t, val);
}

int gsl_rpc_write_uint64(struct gsl_hab_payload *p, uint64_t val)
{
	return GSL_RPC_WRITE_ARG(p, GSL_RPC_64BIT_DATA, uint64_t, val);
}

int grow_data(struct gsl_hab_payload *p, size_t len)
{
	if (p->data != NULL && p->data_size != 0) {
		size_t newSize = ((p->data_size + len) * 3) / 2;
		void *newData = hgsl_malloc(newSize);

		if (newData == NULL) {
			LOGE("No memory allocated\n");
			return -ENOMEM;
		}
		memcpy(newData, p->data, p->data_size);
		hgsl_free(p->data);
		p->data = newData;
		p->data_size = newSize;
	} else {
		size_t newSize = ((4096 + len) * 3) / 2;

		p->data = hgsl_malloc(newSize);
		if (p->data == NULL) {
			LOGE("No memory allocated\n");
			return -ENOMEM;
		}
	}
	return 0;
}

void gsl_hab_payload_dump(struct gsl_hab_payload *p, size_t size_bytes)
{
	char str[HGSL_DUMP_PAYLOAD_STR_SIZE];
	size_t size_dwords = size_bytes / sizeof(uint32_t);
	unsigned int i;
	char *p_str = str;
	int size_left = sizeof(str);
	uint32_t *p_data = (uint32_t *)p->data;

	if (size_dwords > HGSL_MAX_DUMP_PAYLOAD_SIZE)
		size_dwords = HGSL_MAX_DUMP_PAYLOAD_SIZE;
	LOGI("dumping first %d dwords:", size_dwords);

	for (i = 0; i < size_dwords; i++) {
		int c = snprintf(p_str, size_left, "0x%08X ", p_data[i]);

		if ((c < 0) || (c >= size_left))
			break;
		p_str += c;
		size_left -= c;
	}
	LOGI("%s", str);
}

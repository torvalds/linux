/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2013, 2015-2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef GSL_HYP_SOCKET_H
#define GSL_HYP_SOCKET_H

#include <linux/types.h>

#define GSL_HAB_CALL_MAGIC 0xfeedca11
#define GSL_HAB_DATA_MAGIC 0xfeedda7a

#define HAB_INVALID_HANDLE ((int)-1)
#define HGSL_MAX_DUMP_PAYLOAD_SIZE (32)

int gsl_hab_open(int *habfd);
int gsl_hab_recv(int habfd, unsigned char *p, size_t sz, int interruptible);
int gsl_hab_send(int habfd, unsigned char *p, size_t sz);
int gsl_hab_close(int habfd);

struct gsl_hab_payload {
	uint32_t                version;
	size_t                  data_size;
	size_t                  data_pos;
	size_t                  send_size;
	uint8_t                 *data;
};

int gsl_rpc_read(struct gsl_hab_payload *p, void *outData, size_t len);
int gsl_rpc_read_l(struct gsl_hab_payload *p, void **pOutData, size_t len);
int gsl_rpc_read_int32_l(struct gsl_hab_payload *p, int32_t *pArg);
int gsl_rpc_read_uint32_l(struct gsl_hab_payload *p, uint32_t *pArg);
int gsl_rpc_read_int64_l(struct gsl_hab_payload *p, int64_t *pArg);
int gsl_rpc_read_uint64_l(struct gsl_hab_payload *p, uint64_t *pArg);
int gsl_rpc_write(struct gsl_hab_payload *p, const void *data, size_t len);
int gsl_rpc_write_int32(struct gsl_hab_payload *p, int32_t val);
int gsl_rpc_write_uint32(struct gsl_hab_payload *p, uint32_t val);
int gsl_rpc_write_int64(struct gsl_hab_payload *p, int64_t val);
int gsl_rpc_write_uint64(struct gsl_hab_payload *p, uint64_t val);
int gsl_rpc_parcel_init(struct gsl_hab_payload *p);
void gsl_rpc_parcel_free(struct gsl_hab_payload *p);
int gsl_rpc_parcel_reset(struct gsl_hab_payload *p);
int gsl_rpc_parcel_rest_ext(struct gsl_hab_payload *p, uint32_t version);
int gsl_rpc_set_call_params(struct gsl_hab_payload *p,
	uint32_t opcode, uint32_t version);
int gsl_rpc_finalize(struct gsl_hab_payload *p);
int gsl_rpc_get_call_params(struct gsl_hab_payload *p,
	uint32_t *opcode, uint32_t *version);
int gsl_rpc_get_data_params(struct gsl_hab_payload *p,
	void **p_data, uint32_t *size, uint32_t *max_size);
void gsl_hab_payload_dump(struct gsl_hab_payload *p, size_t size);
int grow_data(struct gsl_hab_payload *p, size_t len);
uint32_t gsl_rpc_parcel_get_version(struct gsl_hab_payload *p);
uint32_t gsl_rpc_parcel_get_data_offset(void);

#endif

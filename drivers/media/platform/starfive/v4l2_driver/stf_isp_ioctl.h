/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_ISP_IOCTL_H
#define STF_ISP_IOCTL_H


#include <media/v4l2-ctrls.h>


#define FILENAME_MAX_LEN     30

#define ISP_IOC                         ('V')
#define STF_ISP_REG_BUF_SIZE            (768)
#define STF_ISP_REG_TBL_BUF_SIZE        (STF_ISP_REG_BUF_SIZE / 2)
#define STF_ISP_REG_TBL_2_BUF_SIZE      (STF_ISP_REG_BUF_SIZE / 3)
#define STF_ISP_REG_TBL_3_BUF_SIZE      (STF_ISP_REG_BUF_SIZE / 4)
#define STF_ISP_REG_SMPL_PACK_BUF_SIZE  (STF_ISP_REG_BUF_SIZE / 2)
#define RDMA_WR_ONE                     (0xA0)
#define RDMA_WR_SRL                     (0xA1)
#define RDMA_LINK                       (0xA2)
#define RDMA_SINT                       (0xA3)
#define RDMA_END                        (0xAF)


enum _STF_ISP_IOCTL {
	STF_ISP_IOCTL_LOAD_FW = BASE_VIDIOC_PRIVATE + 1,
	STF_ISP_IOCTL_DMABUF_ALLOC,
	STF_ISP_IOCTL_DMABUF_FREE,
	STF_ISP_IOCTL_GET_HW_VER,
	STF_ISP_IOCTL_REG,
	STF_ISP_IOCTL_SHADOW_LOCK,
	STF_ISP_IOCTL_SHADOW_UNLOCK,
	STF_ISP_IOCTL_SHADOW_UNLOCK_N_TRIGGER,
	STF_ISP_IOCTL_SET_USER_CONFIG_ISP,
	STF_ISP_IOCTL_MAX
};

enum _STF_ISP_REG_METHOD {
	STF_ISP_REG_METHOD_ONE_REG = 0,
	STF_ISP_REG_METHOD_SERIES,
	STF_ISP_REG_METHOD_MODULE,
	STF_ISP_REG_METHOD_TABLE,
	STF_ISP_REG_METHOD_TABLE_2,
	STF_ISP_REG_METHOD_TABLE_3,
	STF_ISP_REG_METHOD_SMPL_PACK,
	STF_ISP_REG_METHOD_SOFT_RDMA,
	STF_ISP_REG_METHOD_MAX
};


struct stfisp_fw_info {
	char __user filename[FILENAME_MAX_LEN];
};

struct dmabuf_create {
	__u32 fd;
	__u32 size;
	__u32 paddr;
};

struct isp_rdma_info {
	u32 param;
	union {
		u32 value;
		struct {
			u32 offset  : 24;
			u32 tag     : 8;
		};
	};
};

struct isp_reg_info {
	/** @brief [in] access method of register */
	u8 method;
	/** @brief [in] offset indicated which register will be read/write */
	u32 offset;
	/** @brief [in] length for indicated how much register will be read/write */
	u32 length;
};

union reg_buf {
	u32 buffer[STF_ISP_REG_BUF_SIZE];
	struct {
		u32 offset;
		u32 value;
	} reg_tbl[STF_ISP_REG_TBL_BUF_SIZE];
	struct {
		u32 offset;
		u32 value;
		u32 mask;
	} reg_tbl2[STF_ISP_REG_TBL_2_BUF_SIZE];
	struct {
		u32 offset;
		u32 value;
		u32 mask;
		u32 delay_ms;
	} reg_tbl3[STF_ISP_REG_TBL_3_BUF_SIZE];
	struct isp_rdma_info rdma_cmd[STF_ISP_REG_SMPL_PACK_BUF_SIZE];
};

struct isp_reg_param {
	/** @brief [in, out] register read/write information */
	struct isp_reg_info reg_info;
	/** @brief [in, out] buffer */
	union reg_buf *reg_buf;
};


#define VIDIOC_STFISP_LOAD_FW \
	_IOW(ISP_IOC, STF_ISP_IOCTL_LOAD_FW, struct stfisp_fw_info)
#define VIDIOC_STF_DMABUF_ALLOC \
	_IOWR(ISP_IOC, STF_ISP_IOCTL_DMABUF_ALLOC, struct dmabuf_create)
#define VIDIOC_STF_DMABUF_FREE \
	_IOWR(ISP_IOC, STF_ISP_IOCTL_DMABUF_FREE, struct dmabuf_create)
#define VIDIOC_STFISP_GET_REG \
	_IOWR(ISP_IOC, STF_ISP_IOCTL_REG, struct isp_reg_param)
#define VIDIOC_STFISP_SET_REG \
	_IOW(ISP_IOC, STF_ISP_IOCTL_REG, struct isp_reg_param)
#define VIDIOC_STFISP_SHADOW_LOCK \
	_IO(ISP_IOC, STF_ISP_IOCTL_SHADOW_LOCK)
#define VIDIOC_STFISP_SHADOW_UNLOCK \
	_IO(ISP_IOC, STF_ISP_IOCTL_SHADOW_UNLOCK)
#define VIDIOC_STFISP_SHADOW_UNLOCK_N_TRIGGER \
	_IO(ISP_IOC, STF_ISP_IOCTL_SHADOW_UNLOCK_N_TRIGGER)
#define VIDIOC_STFISP_SET_USER_CONFIG_ISP \
	_IO(ISP_IOC, STF_ISP_IOCTL_SET_USER_CONFIG_ISP)


#endif /* STF_ISP_IOCTL_H */

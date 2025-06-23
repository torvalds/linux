/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* Copyright 2017 IBM Corp. */
#ifndef _MISC_OCXL_H
#define _MISC_OCXL_H

#include <linux/types.h>
#include <linux/ioctl.h>

enum ocxl_event_type {
	OCXL_AFU_EVENT_XSL_FAULT_ERROR = 0,
};

#define OCXL_KERNEL_EVENT_FLAG_LAST 0x0001  /* This is the last event pending */

struct ocxl_kernel_event_header {
	__u16 type;
	__u16 flags;
	__u32 reserved;
};

struct ocxl_kernel_event_xsl_fault_error {
	__u64 addr;
	__u64 dsisr;
	__u64 count;
	__u64 reserved;
};

struct ocxl_ioctl_attach {
	__u64 amr;
	__u64 reserved1;
	__u64 reserved2;
	__u64 reserved3;
};

struct ocxl_ioctl_metadata {
	__u16 version; /* struct version, always backwards compatible */

	/* Version 0 fields */
	__u8  afu_version_major;
	__u8  afu_version_minor;
	__u32 pasid;		/* PASID assigned to the current context */

	__u64 pp_mmio_size;	/* Per PASID MMIO size */
	__u64 global_mmio_size;

	/* End version 0 fields */

	__u64 reserved[13]; /* Total of 16*u64 */
};

struct ocxl_ioctl_p9_wait {
	__u16 thread_id; /* The thread ID required to wake this thread */
	__u16 reserved1;
	__u32 reserved2;
	__u64 reserved3[3];
};

#define OCXL_IOCTL_FEATURES_FLAGS0_P9_WAIT	0x01
struct ocxl_ioctl_features {
	__u64 flags[4];
};

struct ocxl_ioctl_irq_fd {
	__u64 irq_offset;
	__s32 eventfd;
	__u32 reserved;
};

/* ioctl numbers */
#define OCXL_MAGIC 0xCA
/* AFU devices */
#define OCXL_IOCTL_ATTACH	_IOW(OCXL_MAGIC, 0x10, struct ocxl_ioctl_attach)
#define OCXL_IOCTL_IRQ_ALLOC	_IOR(OCXL_MAGIC, 0x11, __u64)
#define OCXL_IOCTL_IRQ_FREE	_IOW(OCXL_MAGIC, 0x12, __u64)
#define OCXL_IOCTL_IRQ_SET_FD	_IOW(OCXL_MAGIC, 0x13, struct ocxl_ioctl_irq_fd)
#define OCXL_IOCTL_GET_METADATA _IOR(OCXL_MAGIC, 0x14, struct ocxl_ioctl_metadata)
#define OCXL_IOCTL_ENABLE_P9_WAIT	_IOR(OCXL_MAGIC, 0x15, struct ocxl_ioctl_p9_wait)
#define OCXL_IOCTL_GET_FEATURES _IOR(OCXL_MAGIC, 0x16, struct ocxl_ioctl_features)

#endif /* _MISC_OCXL_H */

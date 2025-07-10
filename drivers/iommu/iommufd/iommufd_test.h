/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef _UAPI_IOMMUFD_TEST_H
#define _UAPI_IOMMUFD_TEST_H

#include <linux/iommufd.h>
#include <linux/types.h>

enum {
	IOMMU_TEST_OP_ADD_RESERVED = 1,
	IOMMU_TEST_OP_MOCK_DOMAIN,
	IOMMU_TEST_OP_MD_CHECK_MAP,
	IOMMU_TEST_OP_MD_CHECK_REFS,
	IOMMU_TEST_OP_CREATE_ACCESS,
	IOMMU_TEST_OP_DESTROY_ACCESS_PAGES,
	IOMMU_TEST_OP_ACCESS_PAGES,
	IOMMU_TEST_OP_ACCESS_RW,
	IOMMU_TEST_OP_SET_TEMP_MEMORY_LIMIT,
	IOMMU_TEST_OP_MOCK_DOMAIN_REPLACE,
	IOMMU_TEST_OP_ACCESS_REPLACE_IOAS,
	IOMMU_TEST_OP_MOCK_DOMAIN_FLAGS,
	IOMMU_TEST_OP_DIRTY,
	IOMMU_TEST_OP_MD_CHECK_IOTLB,
	IOMMU_TEST_OP_TRIGGER_IOPF,
	IOMMU_TEST_OP_DEV_CHECK_CACHE,
	IOMMU_TEST_OP_TRIGGER_VEVENT,
	IOMMU_TEST_OP_PASID_ATTACH,
	IOMMU_TEST_OP_PASID_REPLACE,
	IOMMU_TEST_OP_PASID_DETACH,
	IOMMU_TEST_OP_PASID_CHECK_HWPT,
};

enum {
	MOCK_APERTURE_START = 1UL << 24,
	MOCK_APERTURE_LAST = (1UL << 31) - 1,
};

enum {
	MOCK_FLAGS_ACCESS_WRITE = 1 << 0,
	MOCK_FLAGS_ACCESS_SYZ = 1 << 16,
};

enum {
	MOCK_ACCESS_RW_WRITE = 1 << 0,
	MOCK_ACCESS_RW_SLOW_PATH = 1 << 2,
};

enum {
	MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES = 1 << 0,
};

enum {
	MOCK_FLAGS_DEVICE_NO_DIRTY = 1 << 0,
	MOCK_FLAGS_DEVICE_HUGE_IOVA = 1 << 1,
	MOCK_FLAGS_DEVICE_PASID = 1 << 2,
};

enum {
	MOCK_NESTED_DOMAIN_IOTLB_ID_MAX = 3,
	MOCK_NESTED_DOMAIN_IOTLB_NUM = 4,
};

enum {
	MOCK_DEV_CACHE_ID_MAX = 3,
	MOCK_DEV_CACHE_NUM = 4,
};

/* Reserved for special pasid replace test */
#define IOMMU_TEST_PASID_RESERVED 1024

struct iommu_test_cmd {
	__u32 size;
	__u32 op;
	__u32 id;
	__u32 __reserved;
	union {
		struct {
			__aligned_u64 start;
			__aligned_u64 length;
		} add_reserved;
		struct {
			__u32 out_stdev_id;
			__u32 out_hwpt_id;
			/* out_idev_id is the standard iommufd_bind object */
			__u32 out_idev_id;
		} mock_domain;
		struct {
			__u32 out_stdev_id;
			__u32 out_hwpt_id;
			__u32 out_idev_id;
			/* Expand mock_domain to set mock device flags */
			__u32 dev_flags;
		} mock_domain_flags;
		struct {
			__u32 pt_id;
		} mock_domain_replace;
		struct {
			__aligned_u64 iova;
			__aligned_u64 length;
			__aligned_u64 uptr;
		} check_map;
		struct {
			__aligned_u64 length;
			__aligned_u64 uptr;
			__u32 refs;
		} check_refs;
		struct {
			__u32 out_access_fd;
			__u32 flags;
		} create_access;
		struct {
			__u32 access_pages_id;
		} destroy_access_pages;
		struct {
			__u32 flags;
			__u32 out_access_pages_id;
			__aligned_u64 iova;
			__aligned_u64 length;
			__aligned_u64 uptr;
		} access_pages;
		struct {
			__aligned_u64 iova;
			__aligned_u64 length;
			__aligned_u64 uptr;
			__u32 flags;
		} access_rw;
		struct {
			__u32 limit;
		} memory_limit;
		struct {
			__u32 ioas_id;
		} access_replace_ioas;
		struct {
			__u32 flags;
			__aligned_u64 iova;
			__aligned_u64 length;
			__aligned_u64 page_size;
			__aligned_u64 uptr;
			__aligned_u64 out_nr_dirty;
		} dirty;
		struct {
			__u32 id;
			__u32 iotlb;
		} check_iotlb;
		struct {
			__u32 dev_id;
			__u32 pasid;
			__u32 grpid;
			__u32 perm;
			__u64 addr;
		} trigger_iopf;
		struct {
			__u32 id;
			__u32 cache;
		} check_dev_cache;
		struct {
			__u32 dev_id;
		} trigger_vevent;
		struct {
			__u32 pasid;
			__u32 pt_id;
			/* @id is stdev_id */
		} pasid_attach;
		struct {
			__u32 pasid;
			__u32 pt_id;
			/* @id is stdev_id */
		} pasid_replace;
		struct {
			__u32 pasid;
			/* @id is stdev_id */
		} pasid_detach;
		struct {
			__u32 pasid;
			__u32 hwpt_id;
			/* @id is stdev_id */
		} pasid_check;
	};
	__u32 last;
};
#define IOMMU_TEST_CMD _IO(IOMMUFD_TYPE, IOMMUFD_CMD_BASE + 32)

/* Mock device/iommu PASID width */
#define MOCK_PASID_WIDTH 20

/* Mock structs for IOMMU_DEVICE_GET_HW_INFO ioctl */
#define IOMMU_HW_INFO_TYPE_SELFTEST	0xfeedbeef
#define IOMMU_HW_INFO_SELFTEST_REGVAL	0xdeadbeef

struct iommu_test_hw_info {
	__u32 flags;
	__u32 test_reg;
};

/* Should not be equal to any defined value in enum iommu_hwpt_data_type */
#define IOMMU_HWPT_DATA_SELFTEST 0xdead
#define IOMMU_TEST_IOTLB_DEFAULT 0xbadbeef
#define IOMMU_TEST_DEV_CACHE_DEFAULT 0xbaddad

/**
 * struct iommu_hwpt_selftest
 *
 * @iotlb: default mock iotlb value, IOMMU_TEST_IOTLB_DEFAULT
 */
struct iommu_hwpt_selftest {
	__u32 iotlb;
};

/* Should not be equal to any defined value in enum iommu_hwpt_invalidate_data_type */
#define IOMMU_HWPT_INVALIDATE_DATA_SELFTEST 0xdeadbeef
#define IOMMU_HWPT_INVALIDATE_DATA_SELFTEST_INVALID 0xdadbeef

/**
 * struct iommu_hwpt_invalidate_selftest - Invalidation data for Mock driver
 *                                         (IOMMU_HWPT_INVALIDATE_DATA_SELFTEST)
 * @flags: Invalidate flags
 * @iotlb_id: Invalidate iotlb entry index
 *
 * If IOMMU_TEST_INVALIDATE_ALL is set in @flags, @iotlb_id will be ignored
 */
struct iommu_hwpt_invalidate_selftest {
#define IOMMU_TEST_INVALIDATE_FLAG_ALL	(1 << 0)
	__u32 flags;
	__u32 iotlb_id;
};

#define IOMMU_VIOMMU_TYPE_SELFTEST 0xdeadbeef

/**
 * struct iommu_viommu_selftest - vIOMMU data for Mock driver
 *                                (IOMMU_VIOMMU_TYPE_SELFTEST)
 * @in_data: Input random data from user space
 * @out_data: Output data (matching @in_data) to user space
 * @out_mmap_offset: The offset argument for mmap syscall
 * @out_mmap_length: The length argument for mmap syscall
 *
 * Simply set @out_data=@in_data for a loopback test
 */
struct iommu_viommu_selftest {
	__u32 in_data;
	__u32 out_data;
	__aligned_u64 out_mmap_offset;
	__aligned_u64 out_mmap_length;
};

/* Should not be equal to any defined value in enum iommu_viommu_invalidate_data_type */
#define IOMMU_VIOMMU_INVALIDATE_DATA_SELFTEST 0xdeadbeef
#define IOMMU_VIOMMU_INVALIDATE_DATA_SELFTEST_INVALID 0xdadbeef

/**
 * struct iommu_viommu_invalidate_selftest - Invalidation data for Mock VIOMMU
 *                                        (IOMMU_VIOMMU_INVALIDATE_DATA_SELFTEST)
 * @flags: Invalidate flags
 * @cache_id: Invalidate cache entry index
 *
 * If IOMMU_TEST_INVALIDATE_ALL is set in @flags, @cache_id will be ignored
 */
struct iommu_viommu_invalidate_selftest {
#define IOMMU_TEST_INVALIDATE_FLAG_ALL (1 << 0)
	__u32 flags;
	__u32 vdev_id;
	__u32 cache_id;
};

#define IOMMU_VEVENTQ_TYPE_SELFTEST 0xbeefbeef

struct iommu_viommu_event_selftest {
	__u32 virt_id;
};

#define IOMMU_HW_QUEUE_TYPE_SELFTEST 0xdeadbeef
#define IOMMU_TEST_HW_QUEUE_MAX 2

#endif

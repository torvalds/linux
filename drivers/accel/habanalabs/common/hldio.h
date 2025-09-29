/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * hldio.h - NVMe Direct I/O (HLDIO) infrastructure for Habana Labs Driver
 *
 * This feature requires specific hardware setup and must not be built
 * under COMPILE_TEST.
 */

#ifndef __HL_HLDIO_H__
#define __HL_HLDIO_H__

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/ktime.h>     /* ktime functions */
#include <linux/delay.h>     /* usleep_range */
#include <linux/kernel.h>    /* might_sleep_if */
#include <linux/errno.h>     /* error codes */

/* Forward declarations */
struct hl_device;
struct file;

/* Enable only if Kconfig selected */
#ifdef CONFIG_HL_HLDIO
/**
 * struct hl_p2p_region - describes a single P2P memory region
 * @p2ppages: array of page structs for the P2P memory
 * @p2pmem: virtual address of the P2P memory region
 * @device_pa: physical address on the device
 * @bar_offset: offset within the BAR
 * @size: size of the region in bytes
 * @bar: BAR number containing this region
 */
struct hl_p2p_region {
	struct page **p2ppages;
	void *p2pmem;
	u64 device_pa;
	u64 bar_offset;
	u64 size;
	int bar;
};

/**
 * struct hl_dio_stats - Direct I/O statistics
 * @total_ops: total number of operations attempted
 * @successful_ops: number of successful operations
 * @failed_ops: number of failed operations
 * @bytes_transferred: total bytes successfully transferred
 * @last_len_read: length of the last read operation
 */
struct hl_dio_stats {
	u64 total_ops;
	u64 successful_ops;
	u64 failed_ops;
	u64 bytes_transferred;
	size_t last_len_read;
};

/**
 * struct hl_dio - describes habanalabs direct storage interaction interface
 * @p2prs: array of p2p regions
 * @inflight_ios: percpu counter for inflight ios
 * @np2prs: number of elements in p2prs
 * @io_enabled: 1 if io is enabled 0 otherwise
 */
struct hl_dio {
	struct hl_p2p_region *p2prs;
	s64 __percpu *inflight_ios;
	u8 np2prs;
	u8 io_enabled;
};

int hl_dio_ssd2hl(struct hl_device *hdev, struct hl_ctx *ctx, int fd,
		  u64 device_va, off_t off_bytes, size_t len_bytes,
		  size_t *len_read);
void hl_p2p_region_fini_all(struct hl_device *hdev);
int hl_p2p_region_init(struct hl_device *hdev, struct hl_p2p_region *p2pr);
int hl_dio_start(struct hl_device *hdev);
void hl_dio_stop(struct hl_device *hdev);

/* Init/teardown */
int hl_hldio_init(struct hl_device *hdev);
void hl_hldio_fini(struct hl_device *hdev);

/* File operations */
long hl_hldio_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

/* DebugFS hooks */
#ifdef CONFIG_DEBUG_FS
void hl_hldio_debugfs_init(struct hl_device *hdev);
void hl_hldio_debugfs_fini(struct hl_device *hdev);
#else
static inline void hl_hldio_debugfs_init(struct hl_device *hdev) { }
static inline void hl_hldio_debugfs_fini(struct hl_device *hdev) { }
#endif

#else /* !CONFIG_HL_HLDIO */

struct hl_p2p_region;
/* Stubs when HLDIO is disabled */
static inline int hl_dio_ssd2hl(struct hl_device *hdev, struct hl_ctx *ctx, int fd,
		  u64 device_va, off_t off_bytes, size_t len_bytes,
		  size_t *len_read)
{ return -EOPNOTSUPP; }
static inline void hl_p2p_region_fini_all(struct hl_device *hdev) {}
static inline int hl_p2p_region_init(struct hl_device *hdev, struct hl_p2p_region *p2pr)
{ return -EOPNOTSUPP; }
static inline int hl_dio_start(struct hl_device *hdev) { return -EOPNOTSUPP; }
static inline void hl_dio_stop(struct hl_device *hdev) {}

static inline int hl_hldio_init(struct hl_device *hdev) { return 0; }
static inline void hl_hldio_fini(struct hl_device *hdev) { }
static inline long hl_hldio_ioctl(struct file *f, unsigned int c,
				  unsigned long a)
{ return -ENOTTY; }
static inline void hl_hldio_debugfs_init(struct hl_device *hdev) { }
static inline void hl_hldio_debugfs_fini(struct hl_device *hdev) { }

#endif /* CONFIG_HL_HLDIO */

/* Simplified polling macro for HLDIO (no simulator support) */
#define hl_poll_timeout_condition(hdev, cond, sleep_us, timeout_us) \
({ \
	ktime_t __timeout = ktime_add_us(ktime_get(), timeout_us); \
	might_sleep_if(sleep_us); \
	(void)(hdev); /* keep signature consistent, hdev unused */ \
	for (;;) { \
		mb(); /* ensure ordering of memory operations */ \
		if (cond) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) \
			break; \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

#ifdef CONFIG_HL_HLDIO
bool hl_device_supports_nvme(struct hl_device *hdev);
#else
static inline bool hl_device_supports_nvme(struct hl_device *hdev) { return false; }
#endif

#endif /* __HL_HLDIO_H__ */

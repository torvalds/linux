/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004 IBM Corporation
 * Copyright (C) 2015 Intel Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 */

#ifndef __TPM_H__
#define __TPM_H__

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/tpm.h>
#include <linux/tpm_eventlog.h>

#ifdef CONFIG_X86
#include <asm/intel-family.h>
#endif

#define TPM_MINOR		224	/* officially assigned */
#define TPM_BUFSIZE		4096
#define TPM_NUM_DEVICES		65536
#define TPM_RETRY		50

enum tpm_timeout {
	TPM_TIMEOUT = 5,	/* msecs */
	TPM_TIMEOUT_RETRY = 100, /* msecs */
	TPM_TIMEOUT_RANGE_US = 300,	/* usecs */
	TPM_TIMEOUT_POLL = 1,	/* msecs */
	TPM_TIMEOUT_USECS_MIN = 100,      /* usecs */
	TPM_TIMEOUT_USECS_MAX = 500      /* usecs */
};

/* TPM addresses */
enum tpm_addr {
	TPM_SUPERIO_ADDR = 0x2E,
	TPM_ADDR = 0x4E,
};

#define TPM_WARN_RETRY          0x800
#define TPM_WARN_DOING_SELFTEST 0x802
#define TPM_ERR_DEACTIVATED     0x6
#define TPM_ERR_DISABLED        0x7
#define TPM_ERR_INVALID_POSTINIT 38

#define TPM_TAG_RQU_COMMAND 193

/* TPM2 specific constants. */
#define TPM2_SPACE_BUFFER_SIZE		16384 /* 16 kB */

struct	stclear_flags_t {
	__be16	tag;
	u8	deactivated;
	u8	disableForceClear;
	u8	physicalPresence;
	u8	physicalPresenceLock;
	u8	bGlobalLock;
} __packed;

struct tpm1_version {
	u8 major;
	u8 minor;
	u8 rev_major;
	u8 rev_minor;
} __packed;

struct tpm1_version2 {
	__be16 tag;
	struct tpm1_version version;
} __packed;

struct	timeout_t {
	__be32	a;
	__be32	b;
	__be32	c;
	__be32	d;
} __packed;

struct duration_t {
	__be32	tpm_short;
	__be32	tpm_medium;
	__be32	tpm_long;
} __packed;

struct permanent_flags_t {
	__be16	tag;
	u8	disable;
	u8	ownership;
	u8	deactivated;
	u8	readPubek;
	u8	disableOwnerClear;
	u8	allowMaintenance;
	u8	physicalPresenceLifetimeLock;
	u8	physicalPresenceHWEnable;
	u8	physicalPresenceCMDEnable;
	u8	CEKPUsed;
	u8	TPMpost;
	u8	TPMpostLock;
	u8	FIPS;
	u8	operator;
	u8	enableRevokeEK;
	u8	nvLocked;
	u8	readSRKPub;
	u8	tpmEstablished;
	u8	maintenanceDone;
	u8	disableFullDALogicInfo;
} __packed;

typedef union {
	struct	permanent_flags_t perm_flags;
	struct	stclear_flags_t	stclear_flags;
	__u8	owned;
	__be32	num_pcrs;
	struct tpm1_version version1;
	struct tpm1_version2 version2;
	__be32	manufacturer_id;
	struct timeout_t  timeout;
	struct duration_t duration;
} cap_t;

enum tpm_capabilities {
	TPM_CAP_FLAG = 4,
	TPM_CAP_PROP = 5,
	TPM_CAP_VERSION_1_1 = 0x06,
	TPM_CAP_VERSION_1_2 = 0x1A,
};

enum tpm_sub_capabilities {
	TPM_CAP_PROP_PCR = 0x101,
	TPM_CAP_PROP_MANUFACTURER = 0x103,
	TPM_CAP_FLAG_PERM = 0x108,
	TPM_CAP_FLAG_VOL = 0x109,
	TPM_CAP_PROP_OWNER = 0x111,
	TPM_CAP_PROP_TIS_TIMEOUT = 0x115,
	TPM_CAP_PROP_TIS_DURATION = 0x120,
};


/* 128 bytes is an arbitrary cap. This could be as large as TPM_BUFSIZE - 18
 * bytes, but 128 is still a relatively large number of random bytes and
 * anything much bigger causes users of struct tpm_cmd_t to start getting
 * compiler warnings about stack frame size. */
#define TPM_MAX_RNG_DATA	128

extern struct class *tpm_class;
extern struct class *tpmrm_class;
extern dev_t tpm_devt;
extern const struct file_operations tpm_fops;
extern const struct file_operations tpmrm_fops;
extern struct idr dev_nums_idr;

ssize_t tpm_transmit(struct tpm_chip *chip, u8 *buf, size_t bufsiz);
int tpm_get_timeouts(struct tpm_chip *);
int tpm_auto_startup(struct tpm_chip *chip);

int tpm1_pm_suspend(struct tpm_chip *chip, u32 tpm_suspend_pcr);
int tpm1_auto_startup(struct tpm_chip *chip);
int tpm1_do_selftest(struct tpm_chip *chip);
int tpm1_get_timeouts(struct tpm_chip *chip);
unsigned long tpm1_calc_ordinal_duration(struct tpm_chip *chip, u32 ordinal);
int tpm1_pcr_extend(struct tpm_chip *chip, u32 pcr_idx, const u8 *hash,
		    const char *log_msg);
int tpm1_pcr_read(struct tpm_chip *chip, u32 pcr_idx, u8 *res_buf);
ssize_t tpm1_getcap(struct tpm_chip *chip, u32 subcap_id, cap_t *cap,
		    const char *desc, size_t min_cap_length);
int tpm1_get_random(struct tpm_chip *chip, u8 *out, size_t max);
int tpm1_get_pcr_allocation(struct tpm_chip *chip);
unsigned long tpm_calc_ordinal_duration(struct tpm_chip *chip, u32 ordinal);
int tpm_pm_suspend(struct device *dev);
int tpm_pm_resume(struct device *dev);

static inline void tpm_msleep(unsigned int delay_msec)
{
	usleep_range((delay_msec * 1000) - TPM_TIMEOUT_RANGE_US,
		     delay_msec * 1000);
};

int tpm_chip_start(struct tpm_chip *chip);
void tpm_chip_stop(struct tpm_chip *chip);
struct tpm_chip *tpm_find_get_ops(struct tpm_chip *chip);

struct tpm_chip *tpm_chip_alloc(struct device *dev,
				const struct tpm_class_ops *ops);
struct tpm_chip *tpmm_chip_alloc(struct device *pdev,
				 const struct tpm_class_ops *ops);
int tpm_chip_register(struct tpm_chip *chip);
void tpm_chip_unregister(struct tpm_chip *chip);

void tpm_sysfs_add_device(struct tpm_chip *chip);


#ifdef CONFIG_ACPI
extern void tpm_add_ppi(struct tpm_chip *chip);
#else
static inline void tpm_add_ppi(struct tpm_chip *chip)
{
}
#endif

int tpm2_get_timeouts(struct tpm_chip *chip);
int tpm2_pcr_read(struct tpm_chip *chip, u32 pcr_idx,
		  struct tpm_digest *digest, u16 *digest_size_ptr);
int tpm2_pcr_extend(struct tpm_chip *chip, u32 pcr_idx,
		    struct tpm_digest *digests);
int tpm2_get_random(struct tpm_chip *chip, u8 *dest, size_t max);
ssize_t tpm2_get_tpm_pt(struct tpm_chip *chip, u32 property_id,
			u32 *value, const char *desc);

ssize_t tpm2_get_pcr_allocation(struct tpm_chip *chip);
int tpm2_auto_startup(struct tpm_chip *chip);
void tpm2_shutdown(struct tpm_chip *chip, u16 shutdown_type);
unsigned long tpm2_calc_ordinal_duration(struct tpm_chip *chip, u32 ordinal);
int tpm2_probe(struct tpm_chip *chip);
int tpm2_get_cc_attrs_tbl(struct tpm_chip *chip);
int tpm2_find_cc(struct tpm_chip *chip, u32 cc);
int tpm2_init_space(struct tpm_space *space, unsigned int buf_size);
void tpm2_del_space(struct tpm_chip *chip, struct tpm_space *space);
void tpm2_flush_space(struct tpm_chip *chip);
int tpm2_prepare_space(struct tpm_chip *chip, struct tpm_space *space, u8 *cmd,
		       size_t cmdsiz);
int tpm2_commit_space(struct tpm_chip *chip, struct tpm_space *space, void *buf,
		      size_t *bufsiz);
int tpm_devs_add(struct tpm_chip *chip);
void tpm_devs_remove(struct tpm_chip *chip);

void tpm_bios_log_setup(struct tpm_chip *chip);
void tpm_bios_log_teardown(struct tpm_chip *chip);
int tpm_dev_common_init(void);
void tpm_dev_common_exit(void);
#endif

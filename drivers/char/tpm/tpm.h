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
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/tpm.h>
#include <linux/acpi.h>
#include <linux/cdev.h>
#include <linux/highmem.h>

enum tpm_const {
	TPM_MINOR = 224,	/* officially assigned */
	TPM_BUFSIZE = 4096,
	TPM_NUM_DEVICES = 256,
	TPM_RETRY = 50,		/* 5 seconds */
};

enum tpm_timeout {
	TPM_TIMEOUT = 5,	/* msecs */
	TPM_TIMEOUT_RETRY = 100 /* msecs */
};

/* TPM addresses */
enum tpm_addr {
	TPM_SUPERIO_ADDR = 0x2E,
	TPM_ADDR = 0x4E,
};

/* Indexes the duration array */
enum tpm_duration {
	TPM_SHORT = 0,
	TPM_MEDIUM = 1,
	TPM_LONG = 2,
	TPM_UNDEFINED,
};

#define TPM_WARN_RETRY          0x800
#define TPM_WARN_DOING_SELFTEST 0x802
#define TPM_ERR_DEACTIVATED     0x6
#define TPM_ERR_DISABLED        0x7
#define TPM_ERR_INVALID_POSTINIT 38

#define TPM_HEADER_SIZE		10

enum tpm2_const {
	TPM2_PLATFORM_PCR	= 24,
	TPM2_PCR_SELECT_MIN	= ((TPM2_PLATFORM_PCR + 7) / 8),
	TPM2_TIMEOUT_A		= 750,
	TPM2_TIMEOUT_B		= 2000,
	TPM2_TIMEOUT_C		= 200,
	TPM2_TIMEOUT_D		= 30,
	TPM2_DURATION_SHORT	= 20,
	TPM2_DURATION_MEDIUM	= 750,
	TPM2_DURATION_LONG	= 2000,
};

enum tpm2_structures {
	TPM2_ST_NO_SESSIONS	= 0x8001,
	TPM2_ST_SESSIONS	= 0x8002,
};

enum tpm2_return_codes {
	TPM2_RC_INITIALIZE	= 0x0100,
	TPM2_RC_TESTING		= 0x090A,
	TPM2_RC_DISABLED	= 0x0120,
};

enum tpm2_algorithms {
	TPM2_ALG_SHA1		= 0x0004,
	TPM2_ALG_KEYEDHASH	= 0x0008,
	TPM2_ALG_SHA256		= 0x000B,
	TPM2_ALG_NULL		= 0x0010
};

enum tpm2_command_codes {
	TPM2_CC_FIRST		= 0x011F,
	TPM2_CC_SELF_TEST	= 0x0143,
	TPM2_CC_STARTUP		= 0x0144,
	TPM2_CC_SHUTDOWN	= 0x0145,
	TPM2_CC_CREATE		= 0x0153,
	TPM2_CC_LOAD		= 0x0157,
	TPM2_CC_UNSEAL		= 0x015E,
	TPM2_CC_FLUSH_CONTEXT	= 0x0165,
	TPM2_CC_GET_CAPABILITY	= 0x017A,
	TPM2_CC_GET_RANDOM	= 0x017B,
	TPM2_CC_PCR_READ	= 0x017E,
	TPM2_CC_PCR_EXTEND	= 0x0182,
	TPM2_CC_LAST		= 0x018F,
};

enum tpm2_permanent_handles {
	TPM2_RS_PW		= 0x40000009,
};

enum tpm2_capabilities {
	TPM2_CAP_TPM_PROPERTIES = 6,
};

enum tpm2_startup_types {
	TPM2_SU_CLEAR	= 0x0000,
	TPM2_SU_STATE	= 0x0001,
};

enum tpm2_start_method {
	TPM2_START_ACPI = 2,
	TPM2_START_FIFO = 6,
	TPM2_START_CRB = 7,
	TPM2_START_CRB_WITH_ACPI = 8,
};

struct tpm_chip;

struct tpm_vendor_specific {
	void __iomem *iobase;		/* ioremapped address */
	unsigned long base;		/* TPM base address */

	int irq;
	int probed_irq;

	int region_size;
	int have_region;

	struct list_head list;
	int locality;
	unsigned long timeout_a, timeout_b, timeout_c, timeout_d; /* jiffies */
	bool timeout_adjusted;
	unsigned long duration[3]; /* jiffies */
	bool duration_adjusted;
	void *priv;

	wait_queue_head_t read_queue;
	wait_queue_head_t int_queue;

	u16 manufacturer_id;
};

#define TPM_VPRIV(c)     ((c)->vendor.priv)

#define TPM_VID_INTEL    0x8086
#define TPM_VID_WINBOND  0x1050
#define TPM_VID_STM      0x104A

#define TPM_PPI_VERSION_LEN		3

enum tpm_chip_flags {
	TPM_CHIP_FLAG_REGISTERED	= BIT(0),
	TPM_CHIP_FLAG_TPM2		= BIT(1),
};

struct tpm_chip {
	struct device dev;
	struct cdev cdev;

	/* A driver callback under ops cannot be run unless ops_sem is held
	 * (sometimes implicitly, eg for the sysfs code). ops becomes null
	 * when the driver is unregistered, see tpm_try_get_ops.
	 */
	struct rw_semaphore ops_sem;
	const struct tpm_class_ops *ops;

	unsigned int flags;

	int dev_num;		/* /dev/tpm# */
	char devname[7];
	unsigned long is_open;	/* only one allowed */
	int time_expired;

	struct mutex tpm_mutex;	/* tpm is processing */

	struct tpm_vendor_specific vendor;

	struct dentry **bios_dir;

#ifdef CONFIG_ACPI
	const struct attribute_group *groups[2];
	unsigned int groups_cnt;
	acpi_handle acpi_dev_handle;
	char ppi_version[TPM_PPI_VERSION_LEN + 1];
#endif /* CONFIG_ACPI */

	struct list_head list;
};

#define to_tpm_chip(d) container_of(d, struct tpm_chip, dev)

static inline int tpm_read_index(int base, int index)
{
	outb(index, base);
	return inb(base+1) & 0xFF;
}

static inline void tpm_write_index(int base, int index, int value)
{
	outb(index, base);
	outb(value & 0xFF, base+1);
}
struct tpm_input_header {
	__be16	tag;
	__be32	length;
	__be32	ordinal;
} __packed;

struct tpm_output_header {
	__be16	tag;
	__be32	length;
	__be32	return_code;
} __packed;

#define TPM_TAG_RQU_COMMAND cpu_to_be16(193)

struct	stclear_flags_t {
	__be16	tag;
	u8	deactivated;
	u8	disableForceClear;
	u8	physicalPresence;
	u8	physicalPresenceLock;
	u8	bGlobalLock;
} __packed;

struct	tpm_version_t {
	u8	Major;
	u8	Minor;
	u8	revMajor;
	u8	revMinor;
} __packed;

struct	tpm_version_1_2_t {
	__be16	tag;
	u8	Major;
	u8	Minor;
	u8	revMajor;
	u8	revMinor;
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
	bool	owned;
	__be32	num_pcrs;
	struct	tpm_version_t	tpm_version;
	struct	tpm_version_1_2_t tpm_version_1_2;
	__be32	manufacturer_id;
	struct timeout_t  timeout;
	struct duration_t duration;
} cap_t;

enum tpm_capabilities {
	TPM_CAP_FLAG = cpu_to_be32(4),
	TPM_CAP_PROP = cpu_to_be32(5),
	CAP_VERSION_1_1 = cpu_to_be32(0x06),
	CAP_VERSION_1_2 = cpu_to_be32(0x1A)
};

enum tpm_sub_capabilities {
	TPM_CAP_PROP_PCR = cpu_to_be32(0x101),
	TPM_CAP_PROP_MANUFACTURER = cpu_to_be32(0x103),
	TPM_CAP_FLAG_PERM = cpu_to_be32(0x108),
	TPM_CAP_FLAG_VOL = cpu_to_be32(0x109),
	TPM_CAP_PROP_OWNER = cpu_to_be32(0x111),
	TPM_CAP_PROP_TIS_TIMEOUT = cpu_to_be32(0x115),
	TPM_CAP_PROP_TIS_DURATION = cpu_to_be32(0x120),

};

struct	tpm_getcap_params_in {
	__be32	cap;
	__be32	subcap_size;
	__be32	subcap;
} __packed;

struct	tpm_getcap_params_out {
	__be32	cap_size;
	cap_t	cap;
} __packed;

struct	tpm_readpubek_params_out {
	u8	algorithm[4];
	u8	encscheme[2];
	u8	sigscheme[2];
	__be32	paramsize;
	u8	parameters[12]; /*assuming RSA*/
	__be32	keysize;
	u8	modulus[256];
	u8	checksum[20];
} __packed;

typedef union {
	struct	tpm_input_header in;
	struct	tpm_output_header out;
} tpm_cmd_header;

struct tpm_pcrread_out {
	u8	pcr_result[TPM_DIGEST_SIZE];
} __packed;

struct tpm_pcrread_in {
	__be32	pcr_idx;
} __packed;

struct tpm_pcrextend_in {
	__be32	pcr_idx;
	u8	hash[TPM_DIGEST_SIZE];
} __packed;

/* 128 bytes is an arbitrary cap. This could be as large as TPM_BUFSIZE - 18
 * bytes, but 128 is still a relatively large number of random bytes and
 * anything much bigger causes users of struct tpm_cmd_t to start getting
 * compiler warnings about stack frame size. */
#define TPM_MAX_RNG_DATA	128

struct tpm_getrandom_out {
	__be32 rng_data_len;
	u8     rng_data[TPM_MAX_RNG_DATA];
} __packed;

struct tpm_getrandom_in {
	__be32 num_bytes;
} __packed;

struct tpm_startup_in {
	__be16	startup_type;
} __packed;

typedef union {
	struct	tpm_getcap_params_out getcap_out;
	struct	tpm_readpubek_params_out readpubek_out;
	u8	readpubek_out_buffer[sizeof(struct tpm_readpubek_params_out)];
	struct	tpm_getcap_params_in getcap_in;
	struct	tpm_pcrread_in	pcrread_in;
	struct	tpm_pcrread_out	pcrread_out;
	struct	tpm_pcrextend_in pcrextend_in;
	struct	tpm_getrandom_in getrandom_in;
	struct	tpm_getrandom_out getrandom_out;
	struct tpm_startup_in startup_in;
} tpm_cmd_params;

struct tpm_cmd_t {
	tpm_cmd_header	header;
	tpm_cmd_params	params;
} __packed;

/* A string buffer type for constructing TPM commands. This is based on the
 * ideas of string buffer code in security/keys/trusted.h but is heap based
 * in order to keep the stack usage minimal.
 */

enum tpm_buf_flags {
	TPM_BUF_OVERFLOW	= BIT(0),
};

struct tpm_buf {
	struct page *data_page;
	unsigned int flags;
	u8 *data;
};

static inline int tpm_buf_init(struct tpm_buf *buf, u16 tag, u32 ordinal)
{
	struct tpm_input_header *head;

	buf->data_page = alloc_page(GFP_HIGHUSER);
	if (!buf->data_page)
		return -ENOMEM;

	buf->flags = 0;
	buf->data = kmap(buf->data_page);

	head = (struct tpm_input_header *) buf->data;

	head->tag = cpu_to_be16(tag);
	head->length = cpu_to_be32(sizeof(*head));
	head->ordinal = cpu_to_be32(ordinal);

	return 0;
}

static inline void tpm_buf_destroy(struct tpm_buf *buf)
{
	kunmap(buf->data_page);
	__free_page(buf->data_page);
}

static inline u32 tpm_buf_length(struct tpm_buf *buf)
{
	struct tpm_input_header *head = (struct tpm_input_header *) buf->data;

	return be32_to_cpu(head->length);
}

static inline u16 tpm_buf_tag(struct tpm_buf *buf)
{
	struct tpm_input_header *head = (struct tpm_input_header *) buf->data;

	return be16_to_cpu(head->tag);
}

static inline void tpm_buf_append(struct tpm_buf *buf,
				  const unsigned char *new_data,
				  unsigned int new_len)
{
	struct tpm_input_header *head = (struct tpm_input_header *) buf->data;
	u32 len = tpm_buf_length(buf);

	/* Return silently if overflow has already happened. */
	if (buf->flags & TPM_BUF_OVERFLOW)
		return;

	if ((len + new_len) > PAGE_SIZE) {
		WARN(1, "tpm_buf: overflow\n");
		buf->flags |= TPM_BUF_OVERFLOW;
		return;
	}

	memcpy(&buf->data[len], new_data, new_len);
	head->length = cpu_to_be32(len + new_len);
}

static inline void tpm_buf_append_u8(struct tpm_buf *buf, const u8 value)
{
	tpm_buf_append(buf, &value, 1);
}

static inline void tpm_buf_append_u16(struct tpm_buf *buf, const u16 value)
{
	__be16 value2 = cpu_to_be16(value);

	tpm_buf_append(buf, (u8 *) &value2, 2);
}

static inline void tpm_buf_append_u32(struct tpm_buf *buf, const u32 value)
{
	__be32 value2 = cpu_to_be32(value);

	tpm_buf_append(buf, (u8 *) &value2, 4);
}

extern struct class *tpm_class;
extern dev_t tpm_devt;
extern const struct file_operations tpm_fops;

enum tpm_transmit_flags {
	TPM_TRANSMIT_UNLOCKED	= BIT(0),
};

ssize_t tpm_transmit(struct tpm_chip *chip, const u8 *buf, size_t bufsiz,
		     unsigned int flags);
ssize_t tpm_transmit_cmd(struct tpm_chip *chip, const void *cmd, int len,
			 unsigned int flags, const char *desc);
ssize_t	tpm_getcap(struct device *, __be32, cap_t *, const char *);
extern int tpm_get_timeouts(struct tpm_chip *);
extern void tpm_gen_interrupt(struct tpm_chip *);
extern int tpm_do_selftest(struct tpm_chip *);
extern unsigned long tpm_calc_ordinal_duration(struct tpm_chip *, u32);
extern int tpm_pm_suspend(struct device *);
extern int tpm_pm_resume(struct device *);
extern int wait_for_tpm_stat(struct tpm_chip *, u8, unsigned long,
			     wait_queue_head_t *, bool);

struct tpm_chip *tpm_chip_find_get(int chip_num);
__must_check int tpm_try_get_ops(struct tpm_chip *chip);
void tpm_put_ops(struct tpm_chip *chip);

extern struct tpm_chip *tpmm_chip_alloc(struct device *dev,
				       const struct tpm_class_ops *ops);
extern int tpm_chip_register(struct tpm_chip *chip);
extern void tpm_chip_unregister(struct tpm_chip *chip);

int tpm_sysfs_add_device(struct tpm_chip *chip);
void tpm_sysfs_del_device(struct tpm_chip *chip);

int tpm_pcr_read_dev(struct tpm_chip *chip, int pcr_idx, u8 *res_buf);

#ifdef CONFIG_ACPI
extern void tpm_add_ppi(struct tpm_chip *chip);
#else
static inline void tpm_add_ppi(struct tpm_chip *chip)
{
}
#endif

int tpm2_pcr_read(struct tpm_chip *chip, int pcr_idx, u8 *res_buf);
int tpm2_pcr_extend(struct tpm_chip *chip, int pcr_idx, const u8 *hash);
int tpm2_get_random(struct tpm_chip *chip, u8 *out, size_t max);
int tpm2_seal_trusted(struct tpm_chip *chip,
		      struct trusted_key_payload *payload,
		      struct trusted_key_options *options);
int tpm2_unseal_trusted(struct tpm_chip *chip,
			struct trusted_key_payload *payload,
			struct trusted_key_options *options);
ssize_t tpm2_get_tpm_pt(struct tpm_chip *chip, u32 property_id,
			u32 *value, const char *desc);

extern int tpm2_startup(struct tpm_chip *chip, u16 startup_type);
extern void tpm2_shutdown(struct tpm_chip *chip, u16 shutdown_type);
extern unsigned long tpm2_calc_ordinal_duration(struct tpm_chip *, u32);
extern int tpm2_do_selftest(struct tpm_chip *chip);
extern int tpm2_gen_interrupt(struct tpm_chip *chip);
extern int tpm2_probe(struct tpm_chip *chip);

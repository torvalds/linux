/*
 *  drivers/s390/char/sclp_cmd.c
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/completion.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/chpid.h>
#include <asm/sclp.h>
#include "sclp.h"

#define TAG	"sclp_cmd: "

#define SCLP_CMDW_READ_SCP_INFO		0x00020001
#define SCLP_CMDW_READ_SCP_INFO_FORCED	0x00120001

struct read_info_sccb {
	struct	sccb_header header;	/* 0-7 */
	u16	rnmax;			/* 8-9 */
	u8	rnsize;			/* 10 */
	u8	_reserved0[24 - 11];	/* 11-15 */
	u8	loadparm[8];		/* 24-31 */
	u8	_reserved1[48 - 32];	/* 32-47 */
	u64	facilities;		/* 48-55 */
	u8	_reserved2[84 - 56];	/* 56-83 */
	u8	fac84;			/* 84 */
	u8	_reserved3[91 - 85];	/* 85-90 */
	u8	flags;			/* 91 */
	u8	_reserved4[100 - 92];	/* 92-99 */
	u32	rnsize2;		/* 100-103 */
	u64	rnmax2;			/* 104-111 */
	u8	_reserved5[4096 - 112];	/* 112-4095 */
} __attribute__((packed, aligned(PAGE_SIZE)));

static struct read_info_sccb __initdata early_read_info_sccb;
static int __initdata early_read_info_sccb_valid;

u64 sclp_facilities;
static u8 sclp_fac84;

static int __init sclp_cmd_sync_early(sclp_cmdw_t cmd, void *sccb)
{
	int rc;

	__ctl_set_bit(0, 9);
	rc = sclp_service_call(cmd, sccb);
	if (rc)
		goto out;
	__load_psw_mask(PSW_BASE_BITS | PSW_MASK_EXT |
			PSW_MASK_WAIT | PSW_DEFAULT_KEY);
	local_irq_disable();
out:
	/* Contents of the sccb might have changed. */
	barrier();
	__ctl_clear_bit(0, 9);
	return rc;
}

void __init sclp_read_info_early(void)
{
	int rc;
	int i;
	struct read_info_sccb *sccb;
	sclp_cmdw_t commands[] = {SCLP_CMDW_READ_SCP_INFO_FORCED,
				  SCLP_CMDW_READ_SCP_INFO};

	sccb = &early_read_info_sccb;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		do {
			memset(sccb, 0, sizeof(*sccb));
			sccb->header.length = sizeof(*sccb);
			sccb->header.control_mask[2] = 0x80;
			rc = sclp_cmd_sync_early(commands[i], sccb);
		} while (rc == -EBUSY);

		if (rc)
			break;
		if (sccb->header.response_code == 0x10) {
			early_read_info_sccb_valid = 1;
			break;
		}
		if (sccb->header.response_code != 0x1f0)
			break;
	}
}

void __init sclp_facilities_detect(void)
{
	if (!early_read_info_sccb_valid)
		return;
	sclp_facilities = early_read_info_sccb.facilities;
	sclp_fac84 = early_read_info_sccb.fac84;
}

unsigned long long __init sclp_memory_detect(void)
{
	unsigned long long memsize;
	struct read_info_sccb *sccb;

	if (!early_read_info_sccb_valid)
		return 0;
	sccb = &early_read_info_sccb;
	if (sccb->rnsize)
		memsize = sccb->rnsize << 20;
	else
		memsize = sccb->rnsize2 << 20;
	if (sccb->rnmax)
		memsize *= sccb->rnmax;
	else
		memsize *= sccb->rnmax2;
	return memsize;
}

/*
 * This function will be called after sclp_memory_detect(), which gets called
 * early from early.c code. Therefore the sccb should have valid contents.
 */
void __init sclp_get_ipl_info(struct sclp_ipl_info *info)
{
	struct read_info_sccb *sccb;

	if (!early_read_info_sccb_valid)
		return;
	sccb = &early_read_info_sccb;
	info->is_valid = 1;
	if (sccb->flags & 0x2)
		info->has_dump = 1;
	memcpy(&info->loadparm, &sccb->loadparm, LOADPARM_LEN);
}

static void sclp_sync_callback(struct sclp_req *req, void *data)
{
	struct completion *completion = data;

	complete(completion);
}

static int do_sync_request(sclp_cmdw_t cmd, void *sccb)
{
	struct completion completion;
	struct sclp_req *request;
	int rc;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;
	request->command = cmd;
	request->sccb = sccb;
	request->status = SCLP_REQ_FILLED;
	request->callback = sclp_sync_callback;
	request->callback_data = &completion;
	init_completion(&completion);

	/* Perform sclp request. */
	rc = sclp_add_request(request);
	if (rc)
		goto out;
	wait_for_completion(&completion);

	/* Check response. */
	if (request->status != SCLP_REQ_DONE) {
		printk(KERN_WARNING TAG "sync request failed "
		       "(cmd=0x%08x, status=0x%02x)\n", cmd, request->status);
		rc = -EIO;
	}
out:
	kfree(request);
	return rc;
}

/*
 * CPU configuration related functions.
 */

#define SCLP_CMDW_READ_CPU_INFO		0x00010001
#define SCLP_CMDW_CONFIGURE_CPU		0x00110001
#define SCLP_CMDW_DECONFIGURE_CPU	0x00100001

struct read_cpu_info_sccb {
	struct	sccb_header header;
	u16	nr_configured;
	u16	offset_configured;
	u16	nr_standby;
	u16	offset_standby;
	u8	reserved[4096 - 16];
} __attribute__((packed, aligned(PAGE_SIZE)));

static void sclp_fill_cpu_info(struct sclp_cpu_info *info,
			       struct read_cpu_info_sccb *sccb)
{
	char *page = (char *) sccb;

	memset(info, 0, sizeof(*info));
	info->configured = sccb->nr_configured;
	info->standby = sccb->nr_standby;
	info->combined = sccb->nr_configured + sccb->nr_standby;
	info->has_cpu_type = sclp_fac84 & 0x1;
	memcpy(&info->cpu, page + sccb->offset_configured,
	       info->combined * sizeof(struct sclp_cpu_entry));
}

int sclp_get_cpu_info(struct sclp_cpu_info *info)
{
	int rc;
	struct read_cpu_info_sccb *sccb;

	if (!SCLP_HAS_CPU_INFO)
		return -EOPNOTSUPP;
	sccb = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = do_sync_request(SCLP_CMDW_READ_CPU_INFO, sccb);
	if (rc)
		goto out;
	if (sccb->header.response_code != 0x0010) {
		printk(KERN_WARNING TAG "readcpuinfo failed "
		       "(response=0x%04x)\n", sccb->header.response_code);
		rc = -EIO;
		goto out;
	}
	sclp_fill_cpu_info(info, sccb);
out:
	free_page((unsigned long) sccb);
	return rc;
}

struct cpu_configure_sccb {
	struct sccb_header header;
} __attribute__((packed, aligned(8)));

static int do_cpu_configure(sclp_cmdw_t cmd)
{
	struct cpu_configure_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CPU_RECONFIG)
		return -EOPNOTSUPP;
	/*
	 * This is not going to cross a page boundary since we force
	 * kmalloc to have a minimum alignment of 8 bytes on s390.
	 */
	sccb = kzalloc(sizeof(*sccb), GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = do_sync_request(cmd, sccb);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
		break;
	default:
		printk(KERN_WARNING TAG "configure cpu failed (cmd=0x%08x, "
		       "response=0x%04x)\n", cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	kfree(sccb);
	return rc;
}

int sclp_cpu_configure(u8 cpu)
{
	return do_cpu_configure(SCLP_CMDW_CONFIGURE_CPU | cpu << 8);
}

int sclp_cpu_deconfigure(u8 cpu)
{
	return do_cpu_configure(SCLP_CMDW_DECONFIGURE_CPU | cpu << 8);
}

/*
 * Channel path configuration related functions.
 */

#define SCLP_CMDW_CONFIGURE_CHPATH		0x000f0001
#define SCLP_CMDW_DECONFIGURE_CHPATH		0x000e0001
#define SCLP_CMDW_READ_CHPATH_INFORMATION	0x00030001

struct chp_cfg_sccb {
	struct sccb_header header;
	u8 ccm;
	u8 reserved[6];
	u8 cssid;
} __attribute__((packed));

static int do_chp_configure(sclp_cmdw_t cmd)
{
	struct chp_cfg_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CHP_RECONFIG)
		return -EOPNOTSUPP;
	/* Prepare sccb. */
	sccb = (struct chp_cfg_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = do_sync_request(cmd, sccb);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
	case 0x0440:
	case 0x0450:
		break;
	default:
		printk(KERN_WARNING TAG "configure channel-path failed "
		       "(cmd=0x%08x, response=0x%04x)\n", cmd,
		       sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) sccb);
	return rc;
}

/**
 * sclp_chp_configure - perform configure channel-path sclp command
 * @chpid: channel-path ID
 *
 * Perform configure channel-path command sclp command for specified chpid.
 * Return 0 after command successfully finished, non-zero otherwise.
 */
int sclp_chp_configure(struct chp_id chpid)
{
	return do_chp_configure(SCLP_CMDW_CONFIGURE_CHPATH | chpid.id << 8);
}

/**
 * sclp_chp_deconfigure - perform deconfigure channel-path sclp command
 * @chpid: channel-path ID
 *
 * Perform deconfigure channel-path command sclp command for specified chpid
 * and wait for completion. On success return 0. Return non-zero otherwise.
 */
int sclp_chp_deconfigure(struct chp_id chpid)
{
	return do_chp_configure(SCLP_CMDW_DECONFIGURE_CHPATH | chpid.id << 8);
}

struct chp_info_sccb {
	struct sccb_header header;
	u8 recognized[SCLP_CHP_INFO_MASK_SIZE];
	u8 standby[SCLP_CHP_INFO_MASK_SIZE];
	u8 configured[SCLP_CHP_INFO_MASK_SIZE];
	u8 ccm;
	u8 reserved[6];
	u8 cssid;
} __attribute__((packed));

/**
 * sclp_chp_read_info - perform read channel-path information sclp command
 * @info: resulting channel-path information data
 *
 * Perform read channel-path information sclp command and wait for completion.
 * On success, store channel-path information in @info and return 0. Return
 * non-zero otherwise.
 */
int sclp_chp_read_info(struct sclp_chp_info *info)
{
	struct chp_info_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CHP_INFO)
		return -EOPNOTSUPP;
	/* Prepare sccb. */
	sccb = (struct chp_info_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = do_sync_request(SCLP_CMDW_READ_CHPATH_INFORMATION, sccb);
	if (rc)
		goto out;
	if (sccb->header.response_code != 0x0010) {
		printk(KERN_WARNING TAG "read channel-path info failed "
		       "(response=0x%04x)\n", sccb->header.response_code);
		rc = -EIO;
		goto out;
	}
	memcpy(info->recognized, sccb->recognized, SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->standby, sccb->standby, SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->configured, sccb->configured, SCLP_CHP_INFO_MASK_SIZE);
out:
	free_page((unsigned long) sccb);
	return rc;
}

/*
 *  drivers/s390/char/sclp_chp.c
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/completion.h>
#include <asm/sclp.h>
#include <asm/chpid.h>

#include "sclp.h"

#define TAG	"sclp_chp: "

#define SCLP_CMDW_CONFIGURE_CHANNEL_PATH	0x000f0001
#define SCLP_CMDW_DECONFIGURE_CHANNEL_PATH	0x000e0001
#define SCLP_CMDW_READ_CHANNEL_PATH_INFORMATION	0x00030001

static inline sclp_cmdw_t get_configure_cmdw(struct chp_id chpid)
{
	return SCLP_CMDW_CONFIGURE_CHANNEL_PATH | chpid.id << 8;
}

static inline sclp_cmdw_t get_deconfigure_cmdw(struct chp_id chpid)
{
	return SCLP_CMDW_DECONFIGURE_CHANNEL_PATH | chpid.id << 8;
}

static void chp_callback(struct sclp_req *req, void *data)
{
	struct completion *completion = data;

	complete(completion);
}

struct chp_cfg_sccb {
	struct sccb_header header;
	u8 ccm;
	u8 reserved[6];
	u8 cssid;
} __attribute__((packed));

struct chp_cfg_data {
	struct chp_cfg_sccb sccb;
	struct sclp_req req;
	struct completion completion;
} __attribute__((packed));

static int do_configure(sclp_cmdw_t cmd)
{
	struct chp_cfg_data *data;
	int rc;

	if (!SCLP_HAS_CHP_RECONFIG)
		return -EOPNOTSUPP;
	/* Prepare sccb. */
	data = (struct chp_cfg_data *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;
	data->sccb.header.length = sizeof(struct chp_cfg_sccb);
	data->req.command = cmd;
	data->req.sccb = &(data->sccb);
	data->req.status = SCLP_REQ_FILLED;
	data->req.callback = chp_callback;
	data->req.callback_data = &(data->completion);
	init_completion(&data->completion);

	/* Perform sclp request. */
	rc = sclp_add_request(&(data->req));
	if (rc)
		goto out;
	wait_for_completion(&data->completion);

	/* Check response .*/
	if (data->req.status != SCLP_REQ_DONE) {
		printk(KERN_WARNING TAG "configure channel-path request failed "
		       "(status=0x%02x)\n", data->req.status);
		rc = -EIO;
		goto out;
	}
	switch (data->sccb.header.response_code) {
	case 0x0020:
	case 0x0120:
	case 0x0440:
	case 0x0450:
		break;
	default:
		printk(KERN_WARNING TAG "configure channel-path failed "
		       "(cmd=0x%08x, response=0x%04x)\n", cmd,
		       data->sccb.header.response_code);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) data);

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
	return do_configure(get_configure_cmdw(chpid));
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
	return do_configure(get_deconfigure_cmdw(chpid));
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

struct chp_info_data {
	struct chp_info_sccb sccb;
	struct sclp_req req;
	struct completion completion;
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
	struct chp_info_data *data;
	int rc;

	if (!SCLP_HAS_CHP_INFO)
		return -EOPNOTSUPP;
	/* Prepare sccb. */
	data = (struct chp_info_data *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!data)
		return -ENOMEM;
	data->sccb.header.length = sizeof(struct chp_info_sccb);
	data->req.command = SCLP_CMDW_READ_CHANNEL_PATH_INFORMATION;
	data->req.sccb = &(data->sccb);
	data->req.status = SCLP_REQ_FILLED;
	data->req.callback = chp_callback;
	data->req.callback_data = &(data->completion);
	init_completion(&data->completion);

	/* Perform sclp request. */
	rc = sclp_add_request(&(data->req));
	if (rc)
		goto out;
	wait_for_completion(&data->completion);

	/* Check response .*/
	if (data->req.status != SCLP_REQ_DONE) {
		printk(KERN_WARNING TAG "read channel-path info request failed "
		       "(status=0x%02x)\n", data->req.status);
		rc = -EIO;
		goto out;
	}
	if (data->sccb.header.response_code != 0x0010) {
		printk(KERN_WARNING TAG "read channel-path info failed "
		       "(response=0x%04x)\n", data->sccb.header.response_code);
		rc = -EIO;
		goto out;
	}
	memcpy(info->recognized, data->sccb.recognized,
	       SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->standby, data->sccb.standby,
	       SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->configured, data->sccb.configured,
	       SCLP_CHP_INFO_MASK_SIZE);
out:
	free_page((unsigned long) data);

	return rc;
}

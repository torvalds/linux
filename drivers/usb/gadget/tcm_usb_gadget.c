/* Target based USB-Gadget
 *
 * UAS protocol handling, target callbacks, configfs handling,
 * BBB (USB Mass Storage Class Bulk-Only (BBB) and Transport protocol handling.
 *
 * Author: Sebastian Andrzej Siewior <bigeasy at linutronix dot de>
 * License: GPLv2 as published by FSF.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/usb/storage.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_fabric_configfs.h>
#include <target/target_core_configfs.h>
#include <target/configfs_macros.h>
#include <asm/unaligned.h>

#include "usbstring.c"
#include "epautoconf.c"
#include "config.c"
#include "composite.c"

#include "tcm_usb_gadget.h"

static struct target_fabric_configfs *usbg_fabric_configfs;

static inline struct f_uas *to_f_uas(struct usb_function *f)
{
	return container_of(f, struct f_uas, function);
}

static void usbg_cmd_release(struct kref *);

static inline void usbg_cleanup_cmd(struct usbg_cmd *cmd)
{
	kref_put(&cmd->ref, usbg_cmd_release);
}

/* Start bot.c code */

static int bot_enqueue_cmd_cbw(struct f_uas *fu)
{
	int ret;

	if (fu->flags & USBG_BOT_CMD_PEND)
		return 0;

	ret = usb_ep_queue(fu->ep_out, fu->cmd.req, GFP_ATOMIC);
	if (!ret)
		fu->flags |= USBG_BOT_CMD_PEND;
	return ret;
}

static void bot_status_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usbg_cmd *cmd = req->context;
	struct f_uas *fu = cmd->fu;

	usbg_cleanup_cmd(cmd);
	if (req->status < 0) {
		pr_err("ERR %s(%d)\n", __func__, __LINE__);
		return;
	}

	/* CSW completed, wait for next CBW */
	bot_enqueue_cmd_cbw(fu);
}

static void bot_enqueue_sense_code(struct f_uas *fu, struct usbg_cmd *cmd)
{
	struct bulk_cs_wrap *csw = &fu->bot_status.csw;
	int ret;
	u8 *sense;
	unsigned int csw_stat;

	csw_stat = cmd->csw_code;

	/*
	 * We can't send SENSE as a response. So we take ASC & ASCQ from our
	 * sense buffer and queue it and hope the host sends a REQUEST_SENSE
	 * command where it learns why we failed.
	 */
	sense = cmd->sense_iu.sense;

	csw->Tag = cmd->bot_tag;
	csw->Status = csw_stat;
	fu->bot_status.req->context = cmd;
	ret = usb_ep_queue(fu->ep_in, fu->bot_status.req, GFP_ATOMIC);
	if (ret)
		pr_err("%s(%d) ERR: %d\n", __func__, __LINE__, ret);
}

static void bot_err_compl(struct usb_ep *ep, struct usb_request *req)
{
	struct usbg_cmd *cmd = req->context;
	struct f_uas *fu = cmd->fu;

	if (req->status < 0)
		pr_err("ERR %s(%d)\n", __func__, __LINE__);

	if (cmd->data_len) {
		if (cmd->data_len > ep->maxpacket) {
			req->length = ep->maxpacket;
			cmd->data_len -= ep->maxpacket;
		} else {
			req->length = cmd->data_len;
			cmd->data_len = 0;
		}

		usb_ep_queue(ep, req, GFP_ATOMIC);
		return ;
	}
	bot_enqueue_sense_code(fu, cmd);
}

static void bot_send_bad_status(struct usbg_cmd *cmd)
{
	struct f_uas *fu = cmd->fu;
	struct bulk_cs_wrap *csw = &fu->bot_status.csw;
	struct usb_request *req;
	struct usb_ep *ep;

	csw->Residue = cpu_to_le32(cmd->data_len);

	if (cmd->data_len) {
		if (cmd->is_read) {
			ep = fu->ep_in;
			req = fu->bot_req_in;
		} else {
			ep = fu->ep_out;
			req = fu->bot_req_out;
		}

		if (cmd->data_len > fu->ep_in->maxpacket) {
			req->length = ep->maxpacket;
			cmd->data_len -= ep->maxpacket;
		} else {
			req->length = cmd->data_len;
			cmd->data_len = 0;
		}
		req->complete = bot_err_compl;
		req->context = cmd;
		req->buf = fu->cmd.buf;
		usb_ep_queue(ep, req, GFP_KERNEL);
	} else {
		bot_enqueue_sense_code(fu, cmd);
	}
}

static int bot_send_status(struct usbg_cmd *cmd, bool moved_data)
{
	struct f_uas *fu = cmd->fu;
	struct bulk_cs_wrap *csw = &fu->bot_status.csw;
	int ret;

	if (cmd->se_cmd.scsi_status == SAM_STAT_GOOD) {
		if (!moved_data && cmd->data_len) {
			/*
			 * the host wants to move data, we don't. Fill / empty
			 * the pipe and then send the csw with reside set.
			 */
			cmd->csw_code = US_BULK_STAT_OK;
			bot_send_bad_status(cmd);
			return 0;
		}

		csw->Tag = cmd->bot_tag;
		csw->Residue = cpu_to_le32(0);
		csw->Status = US_BULK_STAT_OK;
		fu->bot_status.req->context = cmd;

		ret = usb_ep_queue(fu->ep_in, fu->bot_status.req, GFP_KERNEL);
		if (ret)
			pr_err("%s(%d) ERR: %d\n", __func__, __LINE__, ret);
	} else {
		cmd->csw_code = US_BULK_STAT_FAIL;
		bot_send_bad_status(cmd);
	}
	return 0;
}

/*
 * Called after command (no data transfer) or after the write (to device)
 * operation is completed
 */
static int bot_send_status_response(struct usbg_cmd *cmd)
{
	bool moved_data = false;

	if (!cmd->is_read)
		moved_data = true;
	return bot_send_status(cmd, moved_data);
}

/* Read request completed, now we have to send the CSW */
static void bot_read_compl(struct usb_ep *ep, struct usb_request *req)
{
	struct usbg_cmd *cmd = req->context;

	if (req->status < 0)
		pr_err("ERR %s(%d)\n", __func__, __LINE__);

	bot_send_status(cmd, true);
}

static int bot_send_read_response(struct usbg_cmd *cmd)
{
	struct f_uas *fu = cmd->fu;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct usb_gadget *gadget = fuas_to_gadget(fu);
	int ret;

	if (!cmd->data_len) {
		cmd->csw_code = US_BULK_STAT_PHASE;
		bot_send_bad_status(cmd);
		return 0;
	}

	if (!gadget->sg_supported) {
		cmd->data_buf = kmalloc(se_cmd->data_length, GFP_ATOMIC);
		if (!cmd->data_buf)
			return -ENOMEM;

		sg_copy_to_buffer(se_cmd->t_data_sg,
				se_cmd->t_data_nents,
				cmd->data_buf,
				se_cmd->data_length);

		fu->bot_req_in->buf = cmd->data_buf;
	} else {
		fu->bot_req_in->buf = NULL;
		fu->bot_req_in->num_sgs = se_cmd->t_data_nents;
		fu->bot_req_in->sg = se_cmd->t_data_sg;
	}

	fu->bot_req_in->complete = bot_read_compl;
	fu->bot_req_in->length = se_cmd->data_length;
	fu->bot_req_in->context = cmd;
	ret = usb_ep_queue(fu->ep_in, fu->bot_req_in, GFP_ATOMIC);
	if (ret)
		pr_err("%s(%d)\n", __func__, __LINE__);
	return 0;
}

static void usbg_data_write_cmpl(struct usb_ep *, struct usb_request *);
static int usbg_prepare_w_request(struct usbg_cmd *, struct usb_request *);

static int bot_send_write_request(struct usbg_cmd *cmd)
{
	struct f_uas *fu = cmd->fu;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct usb_gadget *gadget = fuas_to_gadget(fu);
	int ret;

	init_completion(&cmd->write_complete);
	cmd->fu = fu;

	if (!cmd->data_len) {
		cmd->csw_code = US_BULK_STAT_PHASE;
		return -EINVAL;
	}

	if (!gadget->sg_supported) {
		cmd->data_buf = kmalloc(se_cmd->data_length, GFP_KERNEL);
		if (!cmd->data_buf)
			return -ENOMEM;

		fu->bot_req_out->buf = cmd->data_buf;
	} else {
		fu->bot_req_out->buf = NULL;
		fu->bot_req_out->num_sgs = se_cmd->t_data_nents;
		fu->bot_req_out->sg = se_cmd->t_data_sg;
	}

	fu->bot_req_out->complete = usbg_data_write_cmpl;
	fu->bot_req_out->length = se_cmd->data_length;
	fu->bot_req_out->context = cmd;

	ret = usbg_prepare_w_request(cmd, fu->bot_req_out);
	if (ret)
		goto cleanup;
	ret = usb_ep_queue(fu->ep_out, fu->bot_req_out, GFP_KERNEL);
	if (ret)
		pr_err("%s(%d)\n", __func__, __LINE__);

	wait_for_completion(&cmd->write_complete);
	transport_generic_process_write(se_cmd);
cleanup:
	return ret;
}

static int bot_submit_command(struct f_uas *, void *, unsigned int);

static void bot_cmd_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_uas *fu = req->context;
	int ret;

	fu->flags &= ~USBG_BOT_CMD_PEND;

	if (req->status < 0)
		return;

	ret = bot_submit_command(fu, req->buf, req->actual);
	if (ret)
		pr_err("%s(%d): %d\n", __func__, __LINE__, ret);
}

static int bot_prepare_reqs(struct f_uas *fu)
{
	int ret;

	fu->bot_req_in = usb_ep_alloc_request(fu->ep_in, GFP_KERNEL);
	if (!fu->bot_req_in)
		goto err;

	fu->bot_req_out = usb_ep_alloc_request(fu->ep_out, GFP_KERNEL);
	if (!fu->bot_req_out)
		goto err_out;

	fu->cmd.req = usb_ep_alloc_request(fu->ep_out, GFP_KERNEL);
	if (!fu->cmd.req)
		goto err_cmd;

	fu->bot_status.req = usb_ep_alloc_request(fu->ep_in, GFP_KERNEL);
	if (!fu->bot_status.req)
		goto err_sts;

	fu->bot_status.req->buf = &fu->bot_status.csw;
	fu->bot_status.req->length = US_BULK_CS_WRAP_LEN;
	fu->bot_status.req->complete = bot_status_complete;
	fu->bot_status.csw.Signature = cpu_to_le32(US_BULK_CS_SIGN);

	fu->cmd.buf = kmalloc(fu->ep_out->maxpacket, GFP_KERNEL);
	if (!fu->cmd.buf)
		goto err_buf;

	fu->cmd.req->complete = bot_cmd_complete;
	fu->cmd.req->buf = fu->cmd.buf;
	fu->cmd.req->length = fu->ep_out->maxpacket;
	fu->cmd.req->context = fu;

	ret = bot_enqueue_cmd_cbw(fu);
	if (ret)
		goto err_queue;
	return 0;
err_queue:
	kfree(fu->cmd.buf);
	fu->cmd.buf = NULL;
err_buf:
	usb_ep_free_request(fu->ep_in, fu->bot_status.req);
err_sts:
	usb_ep_free_request(fu->ep_out, fu->cmd.req);
	fu->cmd.req = NULL;
err_cmd:
	usb_ep_free_request(fu->ep_out, fu->bot_req_out);
	fu->bot_req_out = NULL;
err_out:
	usb_ep_free_request(fu->ep_in, fu->bot_req_in);
	fu->bot_req_in = NULL;
err:
	pr_err("BOT: endpoint setup failed\n");
	return -ENOMEM;
}

void bot_cleanup_old_alt(struct f_uas *fu)
{
	if (!(fu->flags & USBG_ENABLED))
		return;

	usb_ep_disable(fu->ep_in);
	usb_ep_disable(fu->ep_out);

	if (!fu->bot_req_in)
		return;

	usb_ep_free_request(fu->ep_in, fu->bot_req_in);
	usb_ep_free_request(fu->ep_out, fu->bot_req_out);
	usb_ep_free_request(fu->ep_out, fu->cmd.req);
	usb_ep_free_request(fu->ep_out, fu->bot_status.req);

	kfree(fu->cmd.buf);

	fu->bot_req_in = NULL;
	fu->bot_req_out = NULL;
	fu->cmd.req = NULL;
	fu->bot_status.req = NULL;
	fu->cmd.buf = NULL;
}

static void bot_set_alt(struct f_uas *fu)
{
	struct usb_function *f = &fu->function;
	struct usb_gadget *gadget = f->config->cdev->gadget;
	int ret;

	fu->flags = USBG_IS_BOT;

	config_ep_by_speed(gadget, f, fu->ep_in);
	ret = usb_ep_enable(fu->ep_in);
	if (ret)
		goto err_b_in;

	config_ep_by_speed(gadget, f, fu->ep_out);
	ret = usb_ep_enable(fu->ep_out);
	if (ret)
		goto err_b_out;

	ret = bot_prepare_reqs(fu);
	if (ret)
		goto err_wq;
	fu->flags |= USBG_ENABLED;
	pr_info("Using the BOT protocol\n");
	return;
err_wq:
	usb_ep_disable(fu->ep_out);
err_b_out:
	usb_ep_disable(fu->ep_in);
err_b_in:
	fu->flags = USBG_IS_BOT;
}

static int usbg_bot_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_uas *fu = to_f_uas(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);
	int luns;
	u8 *ret_lun;

	switch (ctrl->bRequest) {
	case US_BULK_GET_MAX_LUN:
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_CLASS |
					USB_RECIP_INTERFACE))
			return -ENOTSUPP;

		if (w_length < 1)
			return -EINVAL;
		if (w_value != 0)
			return -EINVAL;
		luns = atomic_read(&fu->tpg->tpg_port_count);
		if (!luns) {
			pr_err("No LUNs configured?\n");
			return -EINVAL;
		}
		/*
		 * If 4 LUNs are present we return 3 i.e. LUN 0..3 can be
		 * accessed. The upper limit is 0xf
		 */
		luns--;
		if (luns > 0xf) {
			pr_info_once("Limiting the number of luns to 16\n");
			luns = 0xf;
		}
		ret_lun = cdev->req->buf;
		*ret_lun = luns;
		cdev->req->length = 1;
		return usb_ep_queue(cdev->gadget->ep0, cdev->req, GFP_ATOMIC);
		break;

	case US_BULK_RESET_REQUEST:
		/* XXX maybe we should remove previous requests for IN + OUT */
		bot_enqueue_cmd_cbw(fu);
		return 0;
		break;
	};
	return -ENOTSUPP;
}

/* Start uas.c code */

static void uasp_cleanup_one_stream(struct f_uas *fu, struct uas_stream *stream)
{
	/* We have either all three allocated or none */
	if (!stream->req_in)
		return;

	usb_ep_free_request(fu->ep_in, stream->req_in);
	usb_ep_free_request(fu->ep_out, stream->req_out);
	usb_ep_free_request(fu->ep_status, stream->req_status);

	stream->req_in = NULL;
	stream->req_out = NULL;
	stream->req_status = NULL;
}

static void uasp_free_cmdreq(struct f_uas *fu)
{
	usb_ep_free_request(fu->ep_cmd, fu->cmd.req);
	kfree(fu->cmd.buf);
	fu->cmd.req = NULL;
	fu->cmd.buf = NULL;
}

static void uasp_cleanup_old_alt(struct f_uas *fu)
{
	int i;

	if (!(fu->flags & USBG_ENABLED))
		return;

	usb_ep_disable(fu->ep_in);
	usb_ep_disable(fu->ep_out);
	usb_ep_disable(fu->ep_status);
	usb_ep_disable(fu->ep_cmd);

	for (i = 0; i < UASP_SS_EP_COMP_NUM_STREAMS; i++)
		uasp_cleanup_one_stream(fu, &fu->stream[i]);
	uasp_free_cmdreq(fu);
}

static void uasp_status_data_cmpl(struct usb_ep *ep, struct usb_request *req);

static int uasp_prepare_r_request(struct usbg_cmd *cmd)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct f_uas *fu = cmd->fu;
	struct usb_gadget *gadget = fuas_to_gadget(fu);
	struct uas_stream *stream = cmd->stream;

	if (!gadget->sg_supported) {
		cmd->data_buf = kmalloc(se_cmd->data_length, GFP_ATOMIC);
		if (!cmd->data_buf)
			return -ENOMEM;

		sg_copy_to_buffer(se_cmd->t_data_sg,
				se_cmd->t_data_nents,
				cmd->data_buf,
				se_cmd->data_length);

		stream->req_in->buf = cmd->data_buf;
	} else {
		stream->req_in->buf = NULL;
		stream->req_in->num_sgs = se_cmd->t_data_nents;
		stream->req_in->sg = se_cmd->t_data_sg;
	}

	stream->req_in->complete = uasp_status_data_cmpl;
	stream->req_in->length = se_cmd->data_length;
	stream->req_in->context = cmd;

	cmd->state = UASP_SEND_STATUS;
	return 0;
}

static void uasp_prepare_status(struct usbg_cmd *cmd)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct sense_iu *iu = &cmd->sense_iu;
	struct uas_stream *stream = cmd->stream;

	cmd->state = UASP_QUEUE_COMMAND;
	iu->iu_id = IU_ID_STATUS;
	iu->tag = cpu_to_be16(cmd->tag);

	/*
	 * iu->status_qual = cpu_to_be16(STATUS QUALIFIER SAM-4. Where R U?);
	 */
	iu->len = cpu_to_be16(se_cmd->scsi_sense_length);
	iu->status = se_cmd->scsi_status;
	stream->req_status->context = cmd;
	stream->req_status->length = se_cmd->scsi_sense_length + 16;
	stream->req_status->buf = iu;
	stream->req_status->complete = uasp_status_data_cmpl;
}

static void uasp_status_data_cmpl(struct usb_ep *ep, struct usb_request *req)
{
	struct usbg_cmd *cmd = req->context;
	struct uas_stream *stream = cmd->stream;
	struct f_uas *fu = cmd->fu;
	int ret;

	if (req->status < 0)
		goto cleanup;

	switch (cmd->state) {
	case UASP_SEND_DATA:
		ret = uasp_prepare_r_request(cmd);
		if (ret)
			goto cleanup;
		ret = usb_ep_queue(fu->ep_in, stream->req_in, GFP_ATOMIC);
		if (ret)
			pr_err("%s(%d) => %d\n", __func__, __LINE__, ret);
		break;

	case UASP_RECEIVE_DATA:
		ret = usbg_prepare_w_request(cmd, stream->req_out);
		if (ret)
			goto cleanup;
		ret = usb_ep_queue(fu->ep_out, stream->req_out, GFP_ATOMIC);
		if (ret)
			pr_err("%s(%d) => %d\n", __func__, __LINE__, ret);
		break;

	case UASP_SEND_STATUS:
		uasp_prepare_status(cmd);
		ret = usb_ep_queue(fu->ep_status, stream->req_status,
				GFP_ATOMIC);
		if (ret)
			pr_err("%s(%d) => %d\n", __func__, __LINE__, ret);
		break;

	case UASP_QUEUE_COMMAND:
		usbg_cleanup_cmd(cmd);
		usb_ep_queue(fu->ep_cmd, fu->cmd.req, GFP_ATOMIC);
		break;

	default:
		BUG();
	};
	return;

cleanup:
	usbg_cleanup_cmd(cmd);
}

static int uasp_send_status_response(struct usbg_cmd *cmd)
{
	struct f_uas *fu = cmd->fu;
	struct uas_stream *stream = cmd->stream;
	struct sense_iu *iu = &cmd->sense_iu;

	iu->tag = cpu_to_be16(cmd->tag);
	stream->req_status->complete = uasp_status_data_cmpl;
	stream->req_status->context = cmd;
	cmd->fu = fu;
	uasp_prepare_status(cmd);
	return usb_ep_queue(fu->ep_status, stream->req_status, GFP_ATOMIC);
}

static int uasp_send_read_response(struct usbg_cmd *cmd)
{
	struct f_uas *fu = cmd->fu;
	struct uas_stream *stream = cmd->stream;
	struct sense_iu *iu = &cmd->sense_iu;
	int ret;

	cmd->fu = fu;

	iu->tag = cpu_to_be16(cmd->tag);
	if (fu->flags & USBG_USE_STREAMS) {

		ret = uasp_prepare_r_request(cmd);
		if (ret)
			goto out;
		ret = usb_ep_queue(fu->ep_in, stream->req_in, GFP_ATOMIC);
		if (ret) {
			pr_err("%s(%d) => %d\n", __func__, __LINE__, ret);
			kfree(cmd->data_buf);
			cmd->data_buf = NULL;
		}

	} else {

		iu->iu_id = IU_ID_READ_READY;
		iu->tag = cpu_to_be16(cmd->tag);

		stream->req_status->complete = uasp_status_data_cmpl;
		stream->req_status->context = cmd;

		cmd->state = UASP_SEND_DATA;
		stream->req_status->buf = iu;
		stream->req_status->length = sizeof(struct iu);

		ret = usb_ep_queue(fu->ep_status, stream->req_status,
				GFP_ATOMIC);
		if (ret)
			pr_err("%s(%d) => %d\n", __func__, __LINE__, ret);
	}
out:
	return ret;
}

static int uasp_send_write_request(struct usbg_cmd *cmd)
{
	struct f_uas *fu = cmd->fu;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct uas_stream *stream = cmd->stream;
	struct sense_iu *iu = &cmd->sense_iu;
	int ret;

	init_completion(&cmd->write_complete);
	cmd->fu = fu;

	iu->tag = cpu_to_be16(cmd->tag);

	if (fu->flags & USBG_USE_STREAMS) {

		ret = usbg_prepare_w_request(cmd, stream->req_out);
		if (ret)
			goto cleanup;
		ret = usb_ep_queue(fu->ep_out, stream->req_out, GFP_ATOMIC);
		if (ret)
			pr_err("%s(%d)\n", __func__, __LINE__);

	} else {

		iu->iu_id = IU_ID_WRITE_READY;
		iu->tag = cpu_to_be16(cmd->tag);

		stream->req_status->complete = uasp_status_data_cmpl;
		stream->req_status->context = cmd;

		cmd->state = UASP_RECEIVE_DATA;
		stream->req_status->buf = iu;
		stream->req_status->length = sizeof(struct iu);

		ret = usb_ep_queue(fu->ep_status, stream->req_status,
				GFP_ATOMIC);
		if (ret)
			pr_err("%s(%d)\n", __func__, __LINE__);
	}

	wait_for_completion(&cmd->write_complete);
	transport_generic_process_write(se_cmd);
cleanup:
	return ret;
}

static int usbg_submit_command(struct f_uas *, void *, unsigned int);

static void uasp_cmd_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_uas *fu = req->context;
	int ret;

	if (req->status < 0)
		return;

	ret = usbg_submit_command(fu, req->buf, req->actual);
	/*
	 * Once we tune for performance enqueue the command req here again so
	 * we can receive a second command while we processing this one. Pay
	 * attention to properly sync STAUS endpoint with DATA IN + OUT so you
	 * don't break HS.
	 */
	if (!ret)
		return;
	usb_ep_queue(fu->ep_cmd, fu->cmd.req, GFP_ATOMIC);
}

static int uasp_alloc_stream_res(struct f_uas *fu, struct uas_stream *stream)
{
	stream->req_in = usb_ep_alloc_request(fu->ep_in, GFP_KERNEL);
	if (!stream->req_in)
		goto out;

	stream->req_out = usb_ep_alloc_request(fu->ep_out, GFP_KERNEL);
	if (!stream->req_out)
		goto err_out;

	stream->req_status = usb_ep_alloc_request(fu->ep_status, GFP_KERNEL);
	if (!stream->req_status)
		goto err_sts;

	return 0;
err_sts:
	usb_ep_free_request(fu->ep_status, stream->req_status);
	stream->req_status = NULL;
err_out:
	usb_ep_free_request(fu->ep_out, stream->req_out);
	stream->req_out = NULL;
out:
	return -ENOMEM;
}

static int uasp_alloc_cmd(struct f_uas *fu)
{
	fu->cmd.req = usb_ep_alloc_request(fu->ep_cmd, GFP_KERNEL);
	if (!fu->cmd.req)
		goto err;

	fu->cmd.buf = kmalloc(fu->ep_cmd->maxpacket, GFP_KERNEL);
	if (!fu->cmd.buf)
		goto err_buf;

	fu->cmd.req->complete = uasp_cmd_complete;
	fu->cmd.req->buf = fu->cmd.buf;
	fu->cmd.req->length = fu->ep_cmd->maxpacket;
	fu->cmd.req->context = fu;
	return 0;

err_buf:
	usb_ep_free_request(fu->ep_cmd, fu->cmd.req);
err:
	return -ENOMEM;
}

static void uasp_setup_stream_res(struct f_uas *fu, int max_streams)
{
	int i;

	for (i = 0; i < max_streams; i++) {
		struct uas_stream *s = &fu->stream[i];

		s->req_in->stream_id = i + 1;
		s->req_out->stream_id = i + 1;
		s->req_status->stream_id = i + 1;
	}
}

static int uasp_prepare_reqs(struct f_uas *fu)
{
	int ret;
	int i;
	int max_streams;

	if (fu->flags & USBG_USE_STREAMS)
		max_streams = UASP_SS_EP_COMP_NUM_STREAMS;
	else
		max_streams = 1;

	for (i = 0; i < max_streams; i++) {
		ret = uasp_alloc_stream_res(fu, &fu->stream[i]);
		if (ret)
			goto err_cleanup;
	}

	ret = uasp_alloc_cmd(fu);
	if (ret)
		goto err_free_stream;
	uasp_setup_stream_res(fu, max_streams);

	ret = usb_ep_queue(fu->ep_cmd, fu->cmd.req, GFP_ATOMIC);
	if (ret)
		goto err_free_stream;

	return 0;

err_free_stream:
	uasp_free_cmdreq(fu);

err_cleanup:
	if (i) {
		do {
			uasp_cleanup_one_stream(fu, &fu->stream[i - 1]);
			i--;
		} while (i);
	}
	pr_err("UASP: endpoint setup failed\n");
	return ret;
}

static void uasp_set_alt(struct f_uas *fu)
{
	struct usb_function *f = &fu->function;
	struct usb_gadget *gadget = f->config->cdev->gadget;
	int ret;

	fu->flags = USBG_IS_UAS;

	if (gadget->speed == USB_SPEED_SUPER)
		fu->flags |= USBG_USE_STREAMS;

	config_ep_by_speed(gadget, f, fu->ep_in);
	ret = usb_ep_enable(fu->ep_in);
	if (ret)
		goto err_b_in;

	config_ep_by_speed(gadget, f, fu->ep_out);
	ret = usb_ep_enable(fu->ep_out);
	if (ret)
		goto err_b_out;

	config_ep_by_speed(gadget, f, fu->ep_cmd);
	ret = usb_ep_enable(fu->ep_cmd);
	if (ret)
		goto err_cmd;
	config_ep_by_speed(gadget, f, fu->ep_status);
	ret = usb_ep_enable(fu->ep_status);
	if (ret)
		goto err_status;

	ret = uasp_prepare_reqs(fu);
	if (ret)
		goto err_wq;
	fu->flags |= USBG_ENABLED;

	pr_info("Using the UAS protocol\n");
	return;
err_wq:
	usb_ep_disable(fu->ep_status);
err_status:
	usb_ep_disable(fu->ep_cmd);
err_cmd:
	usb_ep_disable(fu->ep_out);
err_b_out:
	usb_ep_disable(fu->ep_in);
err_b_in:
	fu->flags = 0;
}

static int get_cmd_dir(const unsigned char *cdb)
{
	int ret;

	switch (cdb[0]) {
	case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
	case INQUIRY:
	case MODE_SENSE:
	case MODE_SENSE_10:
	case SERVICE_ACTION_IN:
	case MAINTENANCE_IN:
	case PERSISTENT_RESERVE_IN:
	case SECURITY_PROTOCOL_IN:
	case ACCESS_CONTROL_IN:
	case REPORT_LUNS:
	case READ_BLOCK_LIMITS:
	case READ_POSITION:
	case READ_CAPACITY:
	case READ_TOC:
	case READ_FORMAT_CAPACITIES:
	case REQUEST_SENSE:
		ret = DMA_FROM_DEVICE;
		break;

	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
	case MODE_SELECT:
	case MODE_SELECT_10:
	case WRITE_VERIFY:
	case WRITE_VERIFY_12:
	case PERSISTENT_RESERVE_OUT:
	case MAINTENANCE_OUT:
	case SECURITY_PROTOCOL_OUT:
	case ACCESS_CONTROL_OUT:
		ret = DMA_TO_DEVICE;
		break;
	case ALLOW_MEDIUM_REMOVAL:
	case TEST_UNIT_READY:
	case SYNCHRONIZE_CACHE:
	case START_STOP:
	case ERASE:
	case REZERO_UNIT:
	case SEEK_10:
	case SPACE:
	case VERIFY:
	case WRITE_FILEMARKS:
		ret = DMA_NONE;
		break;
	default:
		pr_warn("target: Unknown data direction for SCSI Opcode "
				"0x%02x\n", cdb[0]);
		ret = -EINVAL;
	}
	return ret;
}

static void usbg_data_write_cmpl(struct usb_ep *ep, struct usb_request *req)
{
	struct usbg_cmd *cmd = req->context;
	struct se_cmd *se_cmd = &cmd->se_cmd;

	if (req->status < 0) {
		pr_err("%s() state %d transfer failed\n", __func__, cmd->state);
		goto cleanup;
	}

	if (req->num_sgs == 0) {
		sg_copy_from_buffer(se_cmd->t_data_sg,
				se_cmd->t_data_nents,
				cmd->data_buf,
				se_cmd->data_length);
	}

	complete(&cmd->write_complete);
	return;

cleanup:
	usbg_cleanup_cmd(cmd);
}

static int usbg_prepare_w_request(struct usbg_cmd *cmd, struct usb_request *req)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct f_uas *fu = cmd->fu;
	struct usb_gadget *gadget = fuas_to_gadget(fu);

	if (!gadget->sg_supported) {
		cmd->data_buf = kmalloc(se_cmd->data_length, GFP_ATOMIC);
		if (!cmd->data_buf)
			return -ENOMEM;

		req->buf = cmd->data_buf;
	} else {
		req->buf = NULL;
		req->num_sgs = se_cmd->t_data_nents;
		req->sg = se_cmd->t_data_sg;
	}

	req->complete = usbg_data_write_cmpl;
	req->length = se_cmd->data_length;
	req->context = cmd;
	return 0;
}

static int usbg_send_status_response(struct se_cmd *se_cmd)
{
	struct usbg_cmd *cmd = container_of(se_cmd, struct usbg_cmd,
			se_cmd);
	struct f_uas *fu = cmd->fu;

	if (fu->flags & USBG_IS_BOT)
		return bot_send_status_response(cmd);
	else
		return uasp_send_status_response(cmd);
}

static int usbg_send_write_request(struct se_cmd *se_cmd)
{
	struct usbg_cmd *cmd = container_of(se_cmd, struct usbg_cmd,
			se_cmd);
	struct f_uas *fu = cmd->fu;

	if (fu->flags & USBG_IS_BOT)
		return bot_send_write_request(cmd);
	else
		return uasp_send_write_request(cmd);
}

static int usbg_send_read_response(struct se_cmd *se_cmd)
{
	struct usbg_cmd *cmd = container_of(se_cmd, struct usbg_cmd,
			se_cmd);
	struct f_uas *fu = cmd->fu;

	if (fu->flags & USBG_IS_BOT)
		return bot_send_read_response(cmd);
	else
		return uasp_send_read_response(cmd);
}

static void usbg_cmd_work(struct work_struct *work)
{
	struct usbg_cmd *cmd = container_of(work, struct usbg_cmd, work);
	struct se_cmd *se_cmd;
	struct tcm_usbg_nexus *tv_nexus;
	struct usbg_tpg *tpg;
	int dir;

	se_cmd = &cmd->se_cmd;
	tpg = cmd->fu->tpg;
	tv_nexus = tpg->tpg_nexus;
	dir = get_cmd_dir(cmd->cmd_buf);
	if (dir < 0) {
		transport_init_se_cmd(se_cmd,
				tv_nexus->tvn_se_sess->se_tpg->se_tpg_tfo,
				tv_nexus->tvn_se_sess, cmd->data_len, DMA_NONE,
				cmd->prio_attr, cmd->sense_iu.sense);

		transport_send_check_condition_and_sense(se_cmd,
				TCM_UNSUPPORTED_SCSI_OPCODE, 1);
		usbg_cleanup_cmd(cmd);
		return;
	}

	target_submit_cmd(se_cmd, tv_nexus->tvn_se_sess,
			cmd->cmd_buf, cmd->sense_iu.sense, cmd->unpacked_lun,
			0, cmd->prio_attr, dir, TARGET_SCF_UNKNOWN_SIZE);
}

static int usbg_submit_command(struct f_uas *fu,
		void *cmdbuf, unsigned int len)
{
	struct command_iu *cmd_iu = cmdbuf;
	struct usbg_cmd *cmd;
	struct usbg_tpg *tpg;
	struct se_cmd *se_cmd;
	struct tcm_usbg_nexus *tv_nexus;
	u32 cmd_len;
	int ret;

	if (cmd_iu->iu_id != IU_ID_COMMAND) {
		pr_err("Unsupported type %d\n", cmd_iu->iu_id);
		return -EINVAL;
	}

	cmd = kzalloc(sizeof *cmd, GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	cmd->fu = fu;

	/* XXX until I figure out why I can't free in on complete */
	kref_init(&cmd->ref);
	kref_get(&cmd->ref);

	tpg = fu->tpg;
	cmd_len = (cmd_iu->len & ~0x3) + 16;
	if (cmd_len > USBG_MAX_CMD)
		goto err;

	memcpy(cmd->cmd_buf, cmd_iu->cdb, cmd_len);

	cmd->tag = be16_to_cpup(&cmd_iu->tag);
	if (fu->flags & USBG_USE_STREAMS) {
		if (cmd->tag > UASP_SS_EP_COMP_NUM_STREAMS)
			goto err;
		if (!cmd->tag)
			cmd->stream = &fu->stream[0];
		else
			cmd->stream = &fu->stream[cmd->tag - 1];
	} else {
		cmd->stream = &fu->stream[0];
	}

	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus) {
		pr_err("Missing nexus, ignoring command\n");
		goto err;
	}

	switch (cmd_iu->prio_attr & 0x7) {
	case UAS_HEAD_TAG:
		cmd->prio_attr = MSG_HEAD_TAG;
		break;
	case UAS_ORDERED_TAG:
		cmd->prio_attr = MSG_ORDERED_TAG;
		break;
	case UAS_ACA:
		cmd->prio_attr = MSG_ACA_TAG;
		break;
	default:
		pr_debug_once("Unsupported prio_attr: %02x.\n",
				cmd_iu->prio_attr);
	case UAS_SIMPLE_TAG:
		cmd->prio_attr = MSG_SIMPLE_TAG;
		break;
	}

	se_cmd = &cmd->se_cmd;
	cmd->unpacked_lun = scsilun_to_int(&cmd_iu->lun);

	INIT_WORK(&cmd->work, usbg_cmd_work);
	ret = queue_work(tpg->workqueue, &cmd->work);
	if (ret < 0)
		goto err;

	return 0;
err:
	kfree(cmd);
	return -EINVAL;
}

static void bot_cmd_work(struct work_struct *work)
{
	struct usbg_cmd *cmd = container_of(work, struct usbg_cmd, work);
	struct se_cmd *se_cmd;
	struct tcm_usbg_nexus *tv_nexus;
	struct usbg_tpg *tpg;
	int dir;

	se_cmd = &cmd->se_cmd;
	tpg = cmd->fu->tpg;
	tv_nexus = tpg->tpg_nexus;
	dir = get_cmd_dir(cmd->cmd_buf);
	if (dir < 0) {
		transport_init_se_cmd(se_cmd,
				tv_nexus->tvn_se_sess->se_tpg->se_tpg_tfo,
				tv_nexus->tvn_se_sess, cmd->data_len, DMA_NONE,
				cmd->prio_attr, cmd->sense_iu.sense);

		transport_send_check_condition_and_sense(se_cmd,
				TCM_UNSUPPORTED_SCSI_OPCODE, 1);
		usbg_cleanup_cmd(cmd);
		return;
	}

	target_submit_cmd(se_cmd, tv_nexus->tvn_se_sess,
			cmd->cmd_buf, cmd->sense_iu.sense, cmd->unpacked_lun,
			cmd->data_len, cmd->prio_attr, dir, 0);
}

static int bot_submit_command(struct f_uas *fu,
		void *cmdbuf, unsigned int len)
{
	struct bulk_cb_wrap *cbw = cmdbuf;
	struct usbg_cmd *cmd;
	struct usbg_tpg *tpg;
	struct se_cmd *se_cmd;
	struct tcm_usbg_nexus *tv_nexus;
	u32 cmd_len;
	int ret;

	if (cbw->Signature != cpu_to_le32(US_BULK_CB_SIGN)) {
		pr_err("Wrong signature on CBW\n");
		return -EINVAL;
	}
	if (len != 31) {
		pr_err("Wrong length for CBW\n");
		return -EINVAL;
	}

	cmd_len = cbw->Length;
	if (cmd_len < 1 || cmd_len > 16)
		return -EINVAL;

	cmd = kzalloc(sizeof *cmd, GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	cmd->fu = fu;

	/* XXX until I figure out why I can't free in on complete */
	kref_init(&cmd->ref);
	kref_get(&cmd->ref);

	tpg = fu->tpg;

	memcpy(cmd->cmd_buf, cbw->CDB, cmd_len);

	cmd->bot_tag = cbw->Tag;

	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus) {
		pr_err("Missing nexus, ignoring command\n");
		goto err;
	}

	cmd->prio_attr = MSG_SIMPLE_TAG;
	se_cmd = &cmd->se_cmd;
	cmd->unpacked_lun = cbw->Lun;
	cmd->is_read = cbw->Flags & US_BULK_FLAG_IN ? 1 : 0;
	cmd->data_len = le32_to_cpu(cbw->DataTransferLength);

	INIT_WORK(&cmd->work, bot_cmd_work);
	ret = queue_work(tpg->workqueue, &cmd->work);
	if (ret < 0)
		goto err;

	return 0;
err:
	kfree(cmd);
	return -EINVAL;
}

/* Start fabric.c code */

static int usbg_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int usbg_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static char *usbg_get_fabric_name(void)
{
	return "usb_gadget";
}

static u8 usbg_get_fabric_proto_ident(struct se_portal_group *se_tpg)
{
	struct usbg_tpg *tpg = container_of(se_tpg,
				struct usbg_tpg, se_tpg);
	struct usbg_tport *tport = tpg->tport;
	u8 proto_id;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
	default:
		proto_id = sas_get_fabric_proto_ident(se_tpg);
		break;
	}

	return proto_id;
}

static char *usbg_get_fabric_wwn(struct se_portal_group *se_tpg)
{
	struct usbg_tpg *tpg = container_of(se_tpg,
				struct usbg_tpg, se_tpg);
	struct usbg_tport *tport = tpg->tport;

	return &tport->tport_name[0];
}

static u16 usbg_get_tag(struct se_portal_group *se_tpg)
{
	struct usbg_tpg *tpg = container_of(se_tpg,
				struct usbg_tpg, se_tpg);
	return tpg->tport_tpgt;
}

static u32 usbg_get_default_depth(struct se_portal_group *se_tpg)
{
	return 1;
}

static u32 usbg_get_pr_transport_id(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code,
	unsigned char *buf)
{
	struct usbg_tpg *tpg = container_of(se_tpg,
				struct usbg_tpg, se_tpg);
	struct usbg_tport *tport = tpg->tport;
	int ret = 0;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
	default:
		ret = sas_get_pr_transport_id(se_tpg, se_nacl, pr_reg,
					format_code, buf);
		break;
	}

	return ret;
}

static u32 usbg_get_pr_transport_id_len(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code)
{
	struct usbg_tpg *tpg = container_of(se_tpg,
				struct usbg_tpg, se_tpg);
	struct usbg_tport *tport = tpg->tport;
	int ret = 0;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
	default:
		ret = sas_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,
					format_code);
		break;
	}

	return ret;
}

static char *usbg_parse_pr_out_transport_id(
	struct se_portal_group *se_tpg,
	const char *buf,
	u32 *out_tid_len,
	char **port_nexus_ptr)
{
	struct usbg_tpg *tpg = container_of(se_tpg,
				struct usbg_tpg, se_tpg);
	struct usbg_tport *tport = tpg->tport;
	char *tid = NULL;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
	default:
		tid = sas_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,
					port_nexus_ptr);
	}

	return tid;
}

static struct se_node_acl *usbg_alloc_fabric_acl(struct se_portal_group *se_tpg)
{
	struct usbg_nacl *nacl;

	nacl = kzalloc(sizeof(struct usbg_nacl), GFP_KERNEL);
	if (!nacl) {
		printk(KERN_ERR "Unable to alocate struct usbg_nacl\n");
		return NULL;
	}

	return &nacl->se_node_acl;
}

static void usbg_release_fabric_acl(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl)
{
	struct usbg_nacl *nacl = container_of(se_nacl,
			struct usbg_nacl, se_node_acl);
	kfree(nacl);
}

static u32 usbg_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static int usbg_new_cmd(struct se_cmd *se_cmd)
{
	struct usbg_cmd *cmd = container_of(se_cmd, struct usbg_cmd,
			se_cmd);
	int ret;

	ret = target_setup_cmd_from_cdb(se_cmd, cmd->cmd_buf);
	if (ret)
		return ret;

	return transport_generic_map_mem_to_cmd(se_cmd, NULL, 0, NULL, 0);
}

static void usbg_cmd_release(struct kref *ref)
{
	struct usbg_cmd *cmd = container_of(ref, struct usbg_cmd,
			ref);

	transport_generic_free_cmd(&cmd->se_cmd, 0);
}

static void usbg_release_cmd(struct se_cmd *se_cmd)
{
	struct usbg_cmd *cmd = container_of(se_cmd, struct usbg_cmd,
			se_cmd);
	kfree(cmd->data_buf);
	kfree(cmd);
	return;
}

static int usbg_shutdown_session(struct se_session *se_sess)
{
	return 0;
}

static void usbg_close_session(struct se_session *se_sess)
{
	return;
}

static u32 usbg_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

/*
 * XXX Error recovery: return != 0 if we expect writes. Dunno when that could be
 */
static int usbg_write_pending_status(struct se_cmd *se_cmd)
{
	return 0;
}

static void usbg_set_default_node_attrs(struct se_node_acl *nacl)
{
	return;
}

static u32 usbg_get_task_tag(struct se_cmd *se_cmd)
{
	struct usbg_cmd *cmd = container_of(se_cmd, struct usbg_cmd,
			se_cmd);
	struct f_uas *fu = cmd->fu;

	if (fu->flags & USBG_IS_BOT)
		return le32_to_cpu(cmd->bot_tag);
	else
		return cmd->tag;
}

static int usbg_get_cmd_state(struct se_cmd *se_cmd)
{
	return 0;
}

static int usbg_queue_tm_rsp(struct se_cmd *se_cmd)
{
	return 0;
}

static u16 usbg_set_fabric_sense_len(struct se_cmd *se_cmd, u32 sense_length)
{
	return 0;
}

static u16 usbg_get_fabric_sense_len(void)
{
	return 0;
}

static const char *usbg_check_wwn(const char *name)
{
	const char *n;
	unsigned int len;

	n = strstr(name, "naa.");
	if (!n)
		return NULL;
	n += 4;
	len = strlen(n);
	if (len == 0 || len > USBG_NAMELEN - 1)
		return NULL;
	return n;
}

static struct se_node_acl *usbg_make_nodeacl(
	struct se_portal_group *se_tpg,
	struct config_group *group,
	const char *name)
{
	struct se_node_acl *se_nacl, *se_nacl_new;
	struct usbg_nacl *nacl;
	u64 wwpn = 0;
	u32 nexus_depth;
	const char *wnn_name;

	wnn_name = usbg_check_wwn(name);
	if (!wnn_name)
		return ERR_PTR(-EINVAL);
	se_nacl_new = usbg_alloc_fabric_acl(se_tpg);
	if (!(se_nacl_new))
		return ERR_PTR(-ENOMEM);

	nexus_depth = 1;
	/*
	 * se_nacl_new may be released by core_tpg_add_initiator_node_acl()
	 * when converting a NodeACL from demo mode -> explict
	 */
	se_nacl = core_tpg_add_initiator_node_acl(se_tpg, se_nacl_new,
				name, nexus_depth);
	if (IS_ERR(se_nacl)) {
		usbg_release_fabric_acl(se_tpg, se_nacl_new);
		return se_nacl;
	}
	/*
	 * Locate our struct usbg_nacl and set the FC Nport WWPN
	 */
	nacl = container_of(se_nacl, struct usbg_nacl, se_node_acl);
	nacl->iport_wwpn = wwpn;
	snprintf(nacl->iport_name, sizeof(nacl->iport_name), "%s", name);
	return se_nacl;
}

static void usbg_drop_nodeacl(struct se_node_acl *se_acl)
{
	struct usbg_nacl *nacl = container_of(se_acl,
				struct usbg_nacl, se_node_acl);
	core_tpg_del_initiator_node_acl(se_acl->se_tpg, se_acl, 1);
	kfree(nacl);
}

struct usbg_tpg *the_only_tpg_I_currently_have;

static struct se_portal_group *usbg_make_tpg(
	struct se_wwn *wwn,
	struct config_group *group,
	const char *name)
{
	struct usbg_tport *tport = container_of(wwn, struct usbg_tport,
			tport_wwn);
	struct usbg_tpg *tpg;
	unsigned long tpgt;
	int ret;

	if (strstr(name, "tpgt_") != name)
		return ERR_PTR(-EINVAL);
	if (kstrtoul(name + 5, 0, &tpgt) || tpgt > UINT_MAX)
		return ERR_PTR(-EINVAL);
	if (the_only_tpg_I_currently_have) {
		pr_err("Until the gadget framework can't handle multiple\n");
		pr_err("gadgets, you can't do this here.\n");
		return ERR_PTR(-EBUSY);
	}

	tpg = kzalloc(sizeof(struct usbg_tpg), GFP_KERNEL);
	if (!tpg) {
		printk(KERN_ERR "Unable to allocate struct usbg_tpg");
		return ERR_PTR(-ENOMEM);
	}
	mutex_init(&tpg->tpg_mutex);
	atomic_set(&tpg->tpg_port_count, 0);
	tpg->workqueue = alloc_workqueue("tcm_usb_gadget", 0, 1);
	if (!tpg->workqueue) {
		kfree(tpg);
		return NULL;
	}

	tpg->tport = tport;
	tpg->tport_tpgt = tpgt;

	ret = core_tpg_register(&usbg_fabric_configfs->tf_ops, wwn,
				&tpg->se_tpg, tpg,
				TRANSPORT_TPG_TYPE_NORMAL);
	if (ret < 0) {
		destroy_workqueue(tpg->workqueue);
		kfree(tpg);
		return NULL;
	}
	the_only_tpg_I_currently_have = tpg;
	return &tpg->se_tpg;
}

static void usbg_drop_tpg(struct se_portal_group *se_tpg)
{
	struct usbg_tpg *tpg = container_of(se_tpg,
				struct usbg_tpg, se_tpg);

	core_tpg_deregister(se_tpg);
	destroy_workqueue(tpg->workqueue);
	kfree(tpg);
	the_only_tpg_I_currently_have = NULL;
}

static struct se_wwn *usbg_make_tport(
	struct target_fabric_configfs *tf,
	struct config_group *group,
	const char *name)
{
	struct usbg_tport *tport;
	const char *wnn_name;
	u64 wwpn = 0;

	wnn_name = usbg_check_wwn(name);
	if (!wnn_name)
		return ERR_PTR(-EINVAL);

	tport = kzalloc(sizeof(struct usbg_tport), GFP_KERNEL);
	if (!(tport)) {
		printk(KERN_ERR "Unable to allocate struct usbg_tport");
		return ERR_PTR(-ENOMEM);
	}
	tport->tport_wwpn = wwpn;
	snprintf(tport->tport_name, sizeof(tport->tport_name), wnn_name);
	return &tport->tport_wwn;
}

static void usbg_drop_tport(struct se_wwn *wwn)
{
	struct usbg_tport *tport = container_of(wwn,
				struct usbg_tport, tport_wwn);
	kfree(tport);
}

/*
 * If somebody feels like dropping the version property, go ahead.
 */
static ssize_t usbg_wwn_show_attr_version(
	struct target_fabric_configfs *tf,
	char *page)
{
	return sprintf(page, "usb-gadget fabric module\n");
}
TF_WWN_ATTR_RO(usbg, version);

static struct configfs_attribute *usbg_wwn_attrs[] = {
	&usbg_wwn_version.attr,
	NULL,
};

static ssize_t tcm_usbg_tpg_show_enable(
		struct se_portal_group *se_tpg,
		char *page)
{
	struct usbg_tpg  *tpg = container_of(se_tpg, struct usbg_tpg, se_tpg);

	return snprintf(page, PAGE_SIZE, "%u\n", tpg->gadget_connect);
}

static int usbg_attach(struct usbg_tpg *);
static void usbg_detach(struct usbg_tpg *);

static ssize_t tcm_usbg_tpg_store_enable(
		struct se_portal_group *se_tpg,
		const char *page,
		size_t count)
{
	struct usbg_tpg  *tpg = container_of(se_tpg, struct usbg_tpg, se_tpg);
	unsigned long op;
	ssize_t ret;

	ret = kstrtoul(page, 0, &op);
	if (ret < 0)
		return -EINVAL;
	if (op > 1)
		return -EINVAL;

	if (op && tpg->gadget_connect)
		goto out;
	if (!op && !tpg->gadget_connect)
		goto out;

	if (op) {
		ret = usbg_attach(tpg);
		if (ret)
			goto out;
	} else {
		usbg_detach(tpg);
	}
	tpg->gadget_connect = op;
out:
	return count;
}
TF_TPG_BASE_ATTR(tcm_usbg, enable, S_IRUGO | S_IWUSR);

static ssize_t tcm_usbg_tpg_show_nexus(
		struct se_portal_group *se_tpg,
		char *page)
{
	struct usbg_tpg *tpg = container_of(se_tpg, struct usbg_tpg, se_tpg);
	struct tcm_usbg_nexus *tv_nexus;
	ssize_t ret;

	mutex_lock(&tpg->tpg_mutex);
	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus) {
		ret = -ENODEV;
		goto out;
	}
	ret = snprintf(page, PAGE_SIZE, "%s\n",
			tv_nexus->tvn_se_sess->se_node_acl->initiatorname);
out:
	mutex_unlock(&tpg->tpg_mutex);
	return ret;
}

static int tcm_usbg_make_nexus(struct usbg_tpg *tpg, char *name)
{
	struct se_portal_group *se_tpg;
	struct tcm_usbg_nexus *tv_nexus;
	int ret;

	mutex_lock(&tpg->tpg_mutex);
	if (tpg->tpg_nexus) {
		ret = -EEXIST;
		pr_debug("tpg->tpg_nexus already exists\n");
		goto err_unlock;
	}
	se_tpg = &tpg->se_tpg;

	ret = -ENOMEM;
	tv_nexus = kzalloc(sizeof(*tv_nexus), GFP_KERNEL);
	if (!tv_nexus) {
		pr_err("Unable to allocate struct tcm_vhost_nexus\n");
		goto err_unlock;
	}
	tv_nexus->tvn_se_sess = transport_init_session();
	if (IS_ERR(tv_nexus->tvn_se_sess))
		goto err_free;

	/*
	 * Since we are running in 'demo mode' this call with generate a
	 * struct se_node_acl for the tcm_vhost struct se_portal_group with
	 * the SCSI Initiator port name of the passed configfs group 'name'.
	 */
	tv_nexus->tvn_se_sess->se_node_acl = core_tpg_check_initiator_node_acl(
			se_tpg, name);
	if (!tv_nexus->tvn_se_sess->se_node_acl) {
		pr_debug("core_tpg_check_initiator_node_acl() failed"
				" for %s\n", name);
		goto err_session;
	}
	/*
	 * Now register the TCM vHost virtual I_T Nexus as active with the
	 * call to __transport_register_session()
	 */
	__transport_register_session(se_tpg, tv_nexus->tvn_se_sess->se_node_acl,
			tv_nexus->tvn_se_sess, tv_nexus);
	tpg->tpg_nexus = tv_nexus;
	mutex_unlock(&tpg->tpg_mutex);
	return 0;

err_session:
	transport_free_session(tv_nexus->tvn_se_sess);
err_free:
	kfree(tv_nexus);
err_unlock:
	mutex_unlock(&tpg->tpg_mutex);
	return ret;
}

static int tcm_usbg_drop_nexus(struct usbg_tpg *tpg)
{
	struct se_session *se_sess;
	struct tcm_usbg_nexus *tv_nexus;
	int ret = -ENODEV;

	mutex_lock(&tpg->tpg_mutex);
	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus)
		goto out;

	se_sess = tv_nexus->tvn_se_sess;
	if (!se_sess)
		goto out;

	if (atomic_read(&tpg->tpg_port_count)) {
		ret = -EPERM;
		pr_err("Unable to remove Host I_T Nexus with"
				" active TPG port count: %d\n",
				atomic_read(&tpg->tpg_port_count));
		goto out;
	}

	pr_debug("Removing I_T Nexus to Initiator Port: %s\n",
			tv_nexus->tvn_se_sess->se_node_acl->initiatorname);
	/*
	 * Release the SCSI I_T Nexus to the emulated vHost Target Port
	 */
	transport_deregister_session(tv_nexus->tvn_se_sess);
	tpg->tpg_nexus = NULL;

	kfree(tv_nexus);
out:
	mutex_unlock(&tpg->tpg_mutex);
	return 0;
}

static ssize_t tcm_usbg_tpg_store_nexus(
		struct se_portal_group *se_tpg,
		const char *page,
		size_t count)
{
	struct usbg_tpg *tpg = container_of(se_tpg, struct usbg_tpg, se_tpg);
	unsigned char i_port[USBG_NAMELEN], *ptr;
	int ret;

	if (!strncmp(page, "NULL", 4)) {
		ret = tcm_usbg_drop_nexus(tpg);
		return (!ret) ? count : ret;
	}
	if (strlen(page) > USBG_NAMELEN) {
		pr_err("Emulated NAA Sas Address: %s, exceeds"
				" max: %d\n", page, USBG_NAMELEN);
		return -EINVAL;
	}
	snprintf(i_port, USBG_NAMELEN, "%s", page);

	ptr = strstr(i_port, "naa.");
	if (!ptr) {
		pr_err("Missing 'naa.' prefix\n");
		return -EINVAL;
	}

	if (i_port[strlen(i_port) - 1] == '\n')
		i_port[strlen(i_port) - 1] = '\0';

	ret = tcm_usbg_make_nexus(tpg, &i_port[4]);
	if (ret < 0)
		return ret;
	return count;
}
TF_TPG_BASE_ATTR(tcm_usbg, nexus, S_IRUGO | S_IWUSR);

static struct configfs_attribute *usbg_base_attrs[] = {
	&tcm_usbg_tpg_enable.attr,
	&tcm_usbg_tpg_nexus.attr,
	NULL,
};

static int usbg_port_link(struct se_portal_group *se_tpg, struct se_lun *lun)
{
	struct usbg_tpg *tpg = container_of(se_tpg, struct usbg_tpg, se_tpg);

	atomic_inc(&tpg->tpg_port_count);
	smp_mb__after_atomic_inc();
	return 0;
}

static void usbg_port_unlink(struct se_portal_group *se_tpg,
		struct se_lun *se_lun)
{
	struct usbg_tpg *tpg = container_of(se_tpg, struct usbg_tpg, se_tpg);

	atomic_dec(&tpg->tpg_port_count);
	smp_mb__after_atomic_dec();
}

static int usbg_check_stop_free(struct se_cmd *se_cmd)
{
	struct usbg_cmd *cmd = container_of(se_cmd, struct usbg_cmd,
			se_cmd);

	kref_put(&cmd->ref, usbg_cmd_release);
	return 1;
}

static struct target_core_fabric_ops usbg_ops = {
	.get_fabric_name		= usbg_get_fabric_name,
	.get_fabric_proto_ident		= usbg_get_fabric_proto_ident,
	.tpg_get_wwn			= usbg_get_fabric_wwn,
	.tpg_get_tag			= usbg_get_tag,
	.tpg_get_default_depth		= usbg_get_default_depth,
	.tpg_get_pr_transport_id	= usbg_get_pr_transport_id,
	.tpg_get_pr_transport_id_len	= usbg_get_pr_transport_id_len,
	.tpg_parse_pr_out_transport_id	= usbg_parse_pr_out_transport_id,
	.tpg_check_demo_mode		= usbg_check_true,
	.tpg_check_demo_mode_cache	= usbg_check_false,
	.tpg_check_demo_mode_write_protect = usbg_check_false,
	.tpg_check_prod_mode_write_protect = usbg_check_false,
	.tpg_alloc_fabric_acl		= usbg_alloc_fabric_acl,
	.tpg_release_fabric_acl		= usbg_release_fabric_acl,
	.tpg_get_inst_index		= usbg_tpg_get_inst_index,
	.new_cmd_map			= usbg_new_cmd,
	.release_cmd			= usbg_release_cmd,
	.shutdown_session		= usbg_shutdown_session,
	.close_session			= usbg_close_session,
	.sess_get_index			= usbg_sess_get_index,
	.sess_get_initiator_sid		= NULL,
	.write_pending			= usbg_send_write_request,
	.write_pending_status		= usbg_write_pending_status,
	.set_default_node_attributes	= usbg_set_default_node_attrs,
	.get_task_tag			= usbg_get_task_tag,
	.get_cmd_state			= usbg_get_cmd_state,
	.queue_data_in			= usbg_send_read_response,
	.queue_status			= usbg_send_status_response,
	.queue_tm_rsp			= usbg_queue_tm_rsp,
	.get_fabric_sense_len		= usbg_get_fabric_sense_len,
	.set_fabric_sense_len		= usbg_set_fabric_sense_len,
	.check_stop_free		= usbg_check_stop_free,

	.fabric_make_wwn		= usbg_make_tport,
	.fabric_drop_wwn		= usbg_drop_tport,
	.fabric_make_tpg		= usbg_make_tpg,
	.fabric_drop_tpg		= usbg_drop_tpg,
	.fabric_post_link		= usbg_port_link,
	.fabric_pre_unlink		= usbg_port_unlink,
	.fabric_make_np			= NULL,
	.fabric_drop_np			= NULL,
	.fabric_make_nodeacl		= usbg_make_nodeacl,
	.fabric_drop_nodeacl		= usbg_drop_nodeacl,
};

static int usbg_register_configfs(void)
{
	struct target_fabric_configfs *fabric;
	int ret;

	fabric = target_fabric_configfs_init(THIS_MODULE, "usb_gadget");
	if (IS_ERR(fabric)) {
		printk(KERN_ERR "target_fabric_configfs_init() failed\n");
		return PTR_ERR(fabric);
	}

	fabric->tf_ops = usbg_ops;
	TF_CIT_TMPL(fabric)->tfc_wwn_cit.ct_attrs = usbg_wwn_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_base_cit.ct_attrs = usbg_base_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_attrib_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_param_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_np_base_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_base_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_attrib_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_auth_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_param_cit.ct_attrs = NULL;
	ret = target_fabric_configfs_register(fabric);
	if (ret < 0) {
		printk(KERN_ERR "target_fabric_configfs_register() failed"
				" for usb-gadget\n");
		return ret;
	}
	usbg_fabric_configfs = fabric;
	return 0;
};

static void usbg_deregister_configfs(void)
{
	if (!(usbg_fabric_configfs))
		return;

	target_fabric_configfs_deregister(usbg_fabric_configfs);
	usbg_fabric_configfs = NULL;
};

/* Start gadget.c code */

static struct usb_interface_descriptor bot_intf_desc = {
	.bLength =              sizeof(bot_intf_desc),
	.bDescriptorType =      USB_DT_INTERFACE,
	.bAlternateSetting =	0,
	.bNumEndpoints =        2,
	.bAlternateSetting =	USB_G_ALT_INT_BBB,
	.bInterfaceClass =      USB_CLASS_MASS_STORAGE,
	.bInterfaceSubClass =   USB_SC_SCSI,
	.bInterfaceProtocol =   USB_PR_BULK,
	.iInterface =           USB_G_STR_INT_UAS,
};

static struct usb_interface_descriptor uasp_intf_desc = {
	.bLength =		sizeof(uasp_intf_desc),
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	4,
	.bAlternateSetting =	USB_G_ALT_INT_UAS,
	.bInterfaceClass =	USB_CLASS_MASS_STORAGE,
	.bInterfaceSubClass =	USB_SC_SCSI,
	.bInterfaceProtocol =	USB_PR_UAS,
	.iInterface =		USB_G_STR_INT_BBB,
};

static struct usb_endpoint_descriptor uasp_bi_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor uasp_fs_bi_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_pipe_usage_descriptor uasp_bi_pipe_desc = {
	.bLength =		sizeof(uasp_bi_pipe_desc),
	.bDescriptorType =	USB_DT_PIPE_USAGE,
	.bPipeID =		DATA_IN_PIPE_ID,
};

static struct usb_endpoint_descriptor uasp_ss_bi_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor uasp_bi_ep_comp_desc = {
	.bLength =		sizeof(uasp_bi_ep_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst =		0,
	.bmAttributes =		UASP_SS_EP_COMP_LOG_STREAMS,
	.wBytesPerInterval =	0,
};

static struct usb_ss_ep_comp_descriptor bot_bi_ep_comp_desc = {
	.bLength =		sizeof(bot_bi_ep_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst =		0,
};

static struct usb_endpoint_descriptor uasp_bo_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor uasp_fs_bo_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_pipe_usage_descriptor uasp_bo_pipe_desc = {
	.bLength =		sizeof(uasp_bo_pipe_desc),
	.bDescriptorType =	USB_DT_PIPE_USAGE,
	.bPipeID =		DATA_OUT_PIPE_ID,
};

static struct usb_endpoint_descriptor uasp_ss_bo_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(0x400),
};

static struct usb_ss_ep_comp_descriptor uasp_bo_ep_comp_desc = {
	.bLength =		sizeof(uasp_bo_ep_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bmAttributes =		UASP_SS_EP_COMP_LOG_STREAMS,
};

static struct usb_ss_ep_comp_descriptor bot_bo_ep_comp_desc = {
	.bLength =		sizeof(bot_bo_ep_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_endpoint_descriptor uasp_status_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor uasp_fs_status_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_pipe_usage_descriptor uasp_status_pipe_desc = {
	.bLength =		sizeof(uasp_status_pipe_desc),
	.bDescriptorType =	USB_DT_PIPE_USAGE,
	.bPipeID =		STATUS_PIPE_ID,
};

static struct usb_endpoint_descriptor uasp_ss_status_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor uasp_status_in_ep_comp_desc = {
	.bLength =		sizeof(uasp_status_in_ep_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
	.bmAttributes =		UASP_SS_EP_COMP_LOG_STREAMS,
};

static struct usb_endpoint_descriptor uasp_cmd_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor uasp_fs_cmd_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_pipe_usage_descriptor uasp_cmd_pipe_desc = {
	.bLength =		sizeof(uasp_cmd_pipe_desc),
	.bDescriptorType =	USB_DT_PIPE_USAGE,
	.bPipeID =		CMD_PIPE_ID,
};

static struct usb_endpoint_descriptor uasp_ss_cmd_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor uasp_cmd_comp_desc = {
	.bLength =		sizeof(uasp_cmd_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_descriptor_header *uasp_fs_function_desc[] = {
	(struct usb_descriptor_header *) &bot_intf_desc,
	(struct usb_descriptor_header *) &uasp_fs_bi_desc,
	(struct usb_descriptor_header *) &uasp_fs_bo_desc,

	(struct usb_descriptor_header *) &uasp_intf_desc,
	(struct usb_descriptor_header *) &uasp_fs_bi_desc,
	(struct usb_descriptor_header *) &uasp_bi_pipe_desc,
	(struct usb_descriptor_header *) &uasp_fs_bo_desc,
	(struct usb_descriptor_header *) &uasp_bo_pipe_desc,
	(struct usb_descriptor_header *) &uasp_fs_status_desc,
	(struct usb_descriptor_header *) &uasp_status_pipe_desc,
	(struct usb_descriptor_header *) &uasp_fs_cmd_desc,
	(struct usb_descriptor_header *) &uasp_cmd_pipe_desc,
};

static struct usb_descriptor_header *uasp_hs_function_desc[] = {
	(struct usb_descriptor_header *) &bot_intf_desc,
	(struct usb_descriptor_header *) &uasp_bi_desc,
	(struct usb_descriptor_header *) &uasp_bo_desc,

	(struct usb_descriptor_header *) &uasp_intf_desc,
	(struct usb_descriptor_header *) &uasp_bi_desc,
	(struct usb_descriptor_header *) &uasp_bi_pipe_desc,
	(struct usb_descriptor_header *) &uasp_bo_desc,
	(struct usb_descriptor_header *) &uasp_bo_pipe_desc,
	(struct usb_descriptor_header *) &uasp_status_desc,
	(struct usb_descriptor_header *) &uasp_status_pipe_desc,
	(struct usb_descriptor_header *) &uasp_cmd_desc,
	(struct usb_descriptor_header *) &uasp_cmd_pipe_desc,
	NULL,
};

static struct usb_descriptor_header *uasp_ss_function_desc[] = {
	(struct usb_descriptor_header *) &bot_intf_desc,
	(struct usb_descriptor_header *) &uasp_ss_bi_desc,
	(struct usb_descriptor_header *) &bot_bi_ep_comp_desc,
	(struct usb_descriptor_header *) &uasp_ss_bo_desc,
	(struct usb_descriptor_header *) &bot_bo_ep_comp_desc,

	(struct usb_descriptor_header *) &uasp_intf_desc,
	(struct usb_descriptor_header *) &uasp_ss_bi_desc,
	(struct usb_descriptor_header *) &uasp_bi_ep_comp_desc,
	(struct usb_descriptor_header *) &uasp_bi_pipe_desc,
	(struct usb_descriptor_header *) &uasp_ss_bo_desc,
	(struct usb_descriptor_header *) &uasp_bo_ep_comp_desc,
	(struct usb_descriptor_header *) &uasp_bo_pipe_desc,
	(struct usb_descriptor_header *) &uasp_ss_status_desc,
	(struct usb_descriptor_header *) &uasp_status_in_ep_comp_desc,
	(struct usb_descriptor_header *) &uasp_status_pipe_desc,
	(struct usb_descriptor_header *) &uasp_ss_cmd_desc,
	(struct usb_descriptor_header *) &uasp_cmd_comp_desc,
	(struct usb_descriptor_header *) &uasp_cmd_pipe_desc,
	NULL,
};

#define UAS_VENDOR_ID	0x0525	/* NetChip */
#define UAS_PRODUCT_ID	0xa4a5	/* Linux-USB File-backed Storage Gadget */

static struct usb_device_descriptor usbg_device_desc = {
	.bLength =		sizeof(usbg_device_desc),
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.idVendor =		cpu_to_le16(UAS_VENDOR_ID),
	.idProduct =		cpu_to_le16(UAS_PRODUCT_ID),
	.iManufacturer =	USB_G_STR_MANUFACTOR,
	.iProduct =		USB_G_STR_PRODUCT,
	.iSerialNumber =	USB_G_STR_SERIAL,

	.bNumConfigurations =   1,
};

static struct usb_string	usbg_us_strings[] = {
	{ USB_G_STR_MANUFACTOR,	"Target Manufactor"},
	{ USB_G_STR_PRODUCT,	"Target Product"},
	{ USB_G_STR_SERIAL,	"000000000001"},
	{ USB_G_STR_CONFIG,	"default config"},
	{ USB_G_STR_INT_UAS,	"USB Attached SCSI"},
	{ USB_G_STR_INT_BBB,	"Bulk Only Transport"},
	{ },
};

static struct usb_gadget_strings usbg_stringtab = {
	.language = 0x0409,
	.strings = usbg_us_strings,
};

static struct usb_gadget_strings *usbg_strings[] = {
	&usbg_stringtab,
	NULL,
};

static int guas_unbind(struct usb_composite_dev *cdev)
{
	return 0;
}

static struct usb_configuration usbg_config_driver = {
	.label                  = "Linux Target",
	.bConfigurationValue    = 1,
	.iConfiguration		= USB_G_STR_CONFIG,
	.bmAttributes           = USB_CONFIG_ATT_SELFPOWER,
};

static void give_back_ep(struct usb_ep **pep)
{
	struct usb_ep *ep = *pep;
	if (!ep)
		return;
	ep->driver_data = NULL;
}

static int usbg_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_uas		*fu = to_f_uas(f);
	struct usb_gadget	*gadget = c->cdev->gadget;
	struct usb_ep		*ep;
	int			iface;

	iface = usb_interface_id(c, f);
	if (iface < 0)
		return iface;

	bot_intf_desc.bInterfaceNumber = iface;
	uasp_intf_desc.bInterfaceNumber = iface;
	fu->iface = iface;
	ep = usb_ep_autoconfig_ss(gadget, &uasp_ss_bi_desc,
			&uasp_bi_ep_comp_desc);
	if (!ep)
		goto ep_fail;

	ep->driver_data = fu;
	fu->ep_in = ep;

	ep = usb_ep_autoconfig_ss(gadget, &uasp_ss_bo_desc,
			&uasp_bo_ep_comp_desc);
	if (!ep)
		goto ep_fail;
	ep->driver_data = fu;
	fu->ep_out = ep;

	ep = usb_ep_autoconfig_ss(gadget, &uasp_ss_status_desc,
			&uasp_status_in_ep_comp_desc);
	if (!ep)
		goto ep_fail;
	ep->driver_data = fu;
	fu->ep_status = ep;

	ep = usb_ep_autoconfig_ss(gadget, &uasp_ss_cmd_desc,
			&uasp_cmd_comp_desc);
	if (!ep)
		goto ep_fail;
	ep->driver_data = fu;
	fu->ep_cmd = ep;

	/* Assume endpoint addresses are the same for both speeds */
	uasp_bi_desc.bEndpointAddress =	uasp_ss_bi_desc.bEndpointAddress;
	uasp_bo_desc.bEndpointAddress = uasp_ss_bo_desc.bEndpointAddress;
	uasp_status_desc.bEndpointAddress =
		uasp_ss_status_desc.bEndpointAddress;
	uasp_cmd_desc.bEndpointAddress = uasp_ss_cmd_desc.bEndpointAddress;

	uasp_fs_bi_desc.bEndpointAddress = uasp_ss_bi_desc.bEndpointAddress;
	uasp_fs_bo_desc.bEndpointAddress = uasp_ss_bo_desc.bEndpointAddress;
	uasp_fs_status_desc.bEndpointAddress =
		uasp_ss_status_desc.bEndpointAddress;
	uasp_fs_cmd_desc.bEndpointAddress = uasp_ss_cmd_desc.bEndpointAddress;

	return 0;
ep_fail:
	pr_err("Can't claim all required eps\n");

	give_back_ep(&fu->ep_in);
	give_back_ep(&fu->ep_out);
	give_back_ep(&fu->ep_status);
	give_back_ep(&fu->ep_cmd);
	return -ENOTSUPP;
}

static void usbg_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_uas *fu = to_f_uas(f);

	kfree(fu);
}

struct guas_setup_wq {
	struct work_struct work;
	struct f_uas *fu;
	unsigned int alt;
};

static void usbg_delayed_set_alt(struct work_struct *wq)
{
	struct guas_setup_wq *work = container_of(wq, struct guas_setup_wq,
			work);
	struct f_uas *fu = work->fu;
	int alt = work->alt;

	kfree(work);

	if (fu->flags & USBG_IS_BOT)
		bot_cleanup_old_alt(fu);
	if (fu->flags & USBG_IS_UAS)
		uasp_cleanup_old_alt(fu);

	if (alt == USB_G_ALT_INT_BBB)
		bot_set_alt(fu);
	else if (alt == USB_G_ALT_INT_UAS)
		uasp_set_alt(fu);
	usb_composite_setup_continue(fu->function.config->cdev);
}

static int usbg_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_uas *fu = to_f_uas(f);

	if ((alt == USB_G_ALT_INT_BBB) || (alt == USB_G_ALT_INT_UAS)) {
		struct guas_setup_wq *work;

		work = kmalloc(sizeof(*work), GFP_ATOMIC);
		if (!work)
			return -ENOMEM;
		INIT_WORK(&work->work, usbg_delayed_set_alt);
		work->fu = fu;
		work->alt = alt;
		schedule_work(&work->work);
		return USB_GADGET_DELAYED_STATUS;
	}
	return -EOPNOTSUPP;
}

static void usbg_disable(struct usb_function *f)
{
	struct f_uas *fu = to_f_uas(f);

	if (fu->flags & USBG_IS_UAS)
		uasp_cleanup_old_alt(fu);
	else if (fu->flags & USBG_IS_BOT)
		bot_cleanup_old_alt(fu);
	fu->flags = 0;
}

static int usbg_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_uas *fu = to_f_uas(f);

	if (!(fu->flags & USBG_IS_BOT))
		return -EOPNOTSUPP;

	return usbg_bot_setup(f, ctrl);
}

static int usbg_cfg_bind(struct usb_configuration *c)
{
	struct f_uas *fu;
	int ret;

	fu = kzalloc(sizeof(*fu), GFP_KERNEL);
	if (!fu)
		return -ENOMEM;
	fu->function.name = "Target Function";
	fu->function.descriptors = uasp_fs_function_desc;
	fu->function.hs_descriptors = uasp_hs_function_desc;
	fu->function.ss_descriptors = uasp_ss_function_desc;
	fu->function.bind = usbg_bind;
	fu->function.unbind = usbg_unbind;
	fu->function.set_alt = usbg_set_alt;
	fu->function.setup = usbg_setup;
	fu->function.disable = usbg_disable;
	fu->tpg = the_only_tpg_I_currently_have;

	ret = usb_add_function(c, &fu->function);
	if (ret)
		goto err;

	return 0;
err:
	kfree(fu);
	return ret;
}

static int usb_target_bind(struct usb_composite_dev *cdev)
{
	int ret;

	ret = usb_add_config(cdev, &usbg_config_driver,
			usbg_cfg_bind);
	return 0;
}

static struct usb_composite_driver usbg_driver = {
	.name           = "g_target",
	.dev            = &usbg_device_desc,
	.strings        = usbg_strings,
	.max_speed      = USB_SPEED_SUPER,
	.unbind         = guas_unbind,
};

static int usbg_attach(struct usbg_tpg *tpg)
{
	return usb_composite_probe(&usbg_driver, usb_target_bind);
}

static void usbg_detach(struct usbg_tpg *tpg)
{
	usb_composite_unregister(&usbg_driver);
}

static int __init usb_target_gadget_init(void)
{
	int ret;

	ret = usbg_register_configfs();
	return ret;
}
module_init(usb_target_gadget_init);

static void __exit usb_target_gadget_exit(void)
{
	usbg_deregister_configfs();
}
module_exit(usb_target_gadget_exit);

MODULE_AUTHOR("Sebastian Andrzej Siewior <bigeasy@linutronix.de>");
MODULE_DESCRIPTION("usb-gadget fabric");
MODULE_LICENSE("GPL v2");

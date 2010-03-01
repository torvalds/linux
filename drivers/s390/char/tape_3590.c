/*
 *  drivers/s390/char/tape_3590.c
 *    tape device discipline for 3590 tapes.
 *
 *    Copyright IBM Corp. 2001, 2009
 *    Author(s): Stefan Bader <shbader@de.ibm.com>
 *		 Michael Holzheu <holzheu@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#define KMSG_COMPONENT "tape_3590"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <asm/ebcdic.h>

#define TAPE_DBF_AREA	tape_3590_dbf
#define BUFSIZE 512	/* size of buffers for dynamic generated messages */

#include "tape.h"
#include "tape_std.h"
#include "tape_3590.h"

/*
 * Pointer to debug area.
 */
debug_info_t *TAPE_DBF_AREA = NULL;
EXPORT_SYMBOL(TAPE_DBF_AREA);

/*******************************************************************
 * Error Recovery fuctions:
 * - Read Opposite:		 implemented
 * - Read Device (buffered) log: BRA
 * - Read Library log:		 BRA
 * - Swap Devices:		 BRA
 * - Long Busy:			 implemented
 * - Special Intercept:		 BRA
 * - Read Alternate:		 implemented
 *******************************************************************/

static const char *tape_3590_msg[TAPE_3590_MAX_MSG] = {
	[0x00] = "",
	[0x10] = "Lost Sense",
	[0x11] = "Assigned Elsewhere",
	[0x12] = "Allegiance Reset",
	[0x13] = "Shared Access Violation",
	[0x20] = "Command Reject",
	[0x21] = "Configuration Error",
	[0x22] = "Protection Exception",
	[0x23] = "Write Protect",
	[0x24] = "Write Length",
	[0x25] = "Read-Only Format",
	[0x31] = "Beginning of Partition",
	[0x33] = "End of Partition",
	[0x34] = "End of Data",
	[0x35] = "Block not found",
	[0x40] = "Device Intervention",
	[0x41] = "Loader Intervention",
	[0x42] = "Library Intervention",
	[0x50] = "Write Error",
	[0x51] = "Erase Error",
	[0x52] = "Formatting Error",
	[0x53] = "Read Error",
	[0x54] = "Unsupported Format",
	[0x55] = "No Formatting",
	[0x56] = "Positioning lost",
	[0x57] = "Read Length",
	[0x60] = "Unsupported Medium",
	[0x61] = "Medium Length Error",
	[0x62] = "Medium removed",
	[0x64] = "Load Check",
	[0x65] = "Unload Check",
	[0x70] = "Equipment Check",
	[0x71] = "Bus out Check",
	[0x72] = "Protocol Error",
	[0x73] = "Interface Error",
	[0x74] = "Overrun",
	[0x75] = "Halt Signal",
	[0x90] = "Device fenced",
	[0x91] = "Device Path fenced",
	[0xa0] = "Volume misplaced",
	[0xa1] = "Volume inaccessible",
	[0xa2] = "Volume in input",
	[0xa3] = "Volume ejected",
	[0xa4] = "All categories reserved",
	[0xa5] = "Duplicate Volume",
	[0xa6] = "Library Manager Offline",
	[0xa7] = "Library Output Station full",
	[0xa8] = "Vision System non-operational",
	[0xa9] = "Library Manager Equipment Check",
	[0xaa] = "Library Equipment Check",
	[0xab] = "All Library Cells full",
	[0xac] = "No Cleaner Volumes in Library",
	[0xad] = "I/O Station door open",
	[0xae] = "Subsystem environmental alert",
};

static int crypt_supported(struct tape_device *device)
{
	return TAPE390_CRYPT_SUPPORTED(TAPE_3590_CRYPT_INFO(device));
}

static int crypt_enabled(struct tape_device *device)
{
	return TAPE390_CRYPT_ON(TAPE_3590_CRYPT_INFO(device));
}

static void ext_to_int_kekl(struct tape390_kekl *in,
			    struct tape3592_kekl *out)
{
	int i;

	memset(out, 0, sizeof(*out));
	if (in->type == TAPE390_KEKL_TYPE_HASH)
		out->flags |= 0x40;
	if (in->type_on_tape == TAPE390_KEKL_TYPE_HASH)
		out->flags |= 0x80;
	strncpy(out->label, in->label, 64);
	for (i = strlen(in->label); i < sizeof(out->label); i++)
		out->label[i] = ' ';
	ASCEBC(out->label, sizeof(out->label));
}

static void int_to_ext_kekl(struct tape3592_kekl *in,
			    struct tape390_kekl *out)
{
	memset(out, 0, sizeof(*out));
	if(in->flags & 0x40)
		out->type = TAPE390_KEKL_TYPE_HASH;
	else
		out->type = TAPE390_KEKL_TYPE_LABEL;
	if(in->flags & 0x80)
		out->type_on_tape = TAPE390_KEKL_TYPE_HASH;
	else
		out->type_on_tape = TAPE390_KEKL_TYPE_LABEL;
	memcpy(out->label, in->label, sizeof(in->label));
	EBCASC(out->label, sizeof(in->label));
	strim(out->label);
}

static void int_to_ext_kekl_pair(struct tape3592_kekl_pair *in,
				 struct tape390_kekl_pair *out)
{
	if (in->count == 0) {
		out->kekl[0].type = TAPE390_KEKL_TYPE_NONE;
		out->kekl[0].type_on_tape = TAPE390_KEKL_TYPE_NONE;
		out->kekl[1].type = TAPE390_KEKL_TYPE_NONE;
		out->kekl[1].type_on_tape = TAPE390_KEKL_TYPE_NONE;
	} else if (in->count == 1) {
		int_to_ext_kekl(&in->kekl[0], &out->kekl[0]);
		out->kekl[1].type = TAPE390_KEKL_TYPE_NONE;
		out->kekl[1].type_on_tape = TAPE390_KEKL_TYPE_NONE;
	} else if (in->count == 2) {
		int_to_ext_kekl(&in->kekl[0], &out->kekl[0]);
		int_to_ext_kekl(&in->kekl[1], &out->kekl[1]);
	} else {
		printk("Invalid KEKL number: %d\n", in->count);
		BUG();
	}
}

static int check_ext_kekl(struct tape390_kekl *kekl)
{
	if (kekl->type == TAPE390_KEKL_TYPE_NONE)
		goto invalid;
	if (kekl->type > TAPE390_KEKL_TYPE_HASH)
		goto invalid;
	if (kekl->type_on_tape == TAPE390_KEKL_TYPE_NONE)
		goto invalid;
	if (kekl->type_on_tape > TAPE390_KEKL_TYPE_HASH)
		goto invalid;
	if ((kekl->type == TAPE390_KEKL_TYPE_HASH) &&
	    (kekl->type_on_tape == TAPE390_KEKL_TYPE_LABEL))
		goto invalid;

	return 0;
invalid:
	return -EINVAL;
}

static int check_ext_kekl_pair(struct tape390_kekl_pair *kekls)
{
	if (check_ext_kekl(&kekls->kekl[0]))
		goto invalid;
	if (check_ext_kekl(&kekls->kekl[1]))
		goto invalid;

	return 0;
invalid:
	return -EINVAL;
}

/*
 * Query KEKLs
 */
static int tape_3592_kekl_query(struct tape_device *device,
				struct tape390_kekl_pair *ext_kekls)
{
	struct tape_request *request;
	struct tape3592_kekl_query_order *order;
	struct tape3592_kekl_query_data *int_kekls;
	int rc;

	DBF_EVENT(6, "tape3592_kekl_query\n");
	int_kekls = kmalloc(sizeof(*int_kekls), GFP_KERNEL|GFP_DMA);
	if (!int_kekls)
		return -ENOMEM;
	request = tape_alloc_request(2, sizeof(*order));
	if (IS_ERR(request)) {
		rc = PTR_ERR(request);
		goto fail_malloc;
	}
	order = request->cpdata;
	memset(order,0,sizeof(*order));
	order->code = 0xe2;
	order->max_count = 2;
	request->op = TO_KEKL_QUERY;
	tape_ccw_cc(request->cpaddr, PERF_SUBSYS_FUNC, sizeof(*order), order);
	tape_ccw_end(request->cpaddr + 1, READ_SS_DATA, sizeof(*int_kekls),
		     int_kekls);
	rc = tape_do_io(device, request);
	if (rc)
		goto fail_request;
	int_to_ext_kekl_pair(&int_kekls->kekls, ext_kekls);

	rc = 0;
fail_request:
	tape_free_request(request);
fail_malloc:
	kfree(int_kekls);
	return rc;
}

/*
 * IOCTL: Query KEKLs
 */
static int tape_3592_ioctl_kekl_query(struct tape_device *device,
				      unsigned long arg)
{
	int rc;
	struct tape390_kekl_pair *ext_kekls;

	DBF_EVENT(6, "tape_3592_ioctl_kekl_query\n");
	if (!crypt_supported(device))
		return -ENOSYS;
	if (!crypt_enabled(device))
		return -EUNATCH;
	ext_kekls = kmalloc(sizeof(*ext_kekls), GFP_KERNEL);
	if (!ext_kekls)
		return -ENOMEM;
	rc = tape_3592_kekl_query(device, ext_kekls);
	if (rc != 0)
		goto fail;
	if (copy_to_user((char __user *) arg, ext_kekls, sizeof(*ext_kekls))) {
		rc = -EFAULT;
		goto fail;
	}
	rc = 0;
fail:
	kfree(ext_kekls);
	return rc;
}

static int tape_3590_mttell(struct tape_device *device, int mt_count);

/*
 * Set KEKLs
 */
static int tape_3592_kekl_set(struct tape_device *device,
			      struct tape390_kekl_pair *ext_kekls)
{
	struct tape_request *request;
	struct tape3592_kekl_set_order *order;

	DBF_EVENT(6, "tape3592_kekl_set\n");
	if (check_ext_kekl_pair(ext_kekls)) {
		DBF_EVENT(6, "invalid kekls\n");
		return -EINVAL;
	}
	if (tape_3590_mttell(device, 0) != 0)
		return -EBADSLT;
	request = tape_alloc_request(1, sizeof(*order));
	if (IS_ERR(request))
		return PTR_ERR(request);
	order = request->cpdata;
	memset(order, 0, sizeof(*order));
	order->code = 0xe3;
	order->kekls.count = 2;
	ext_to_int_kekl(&ext_kekls->kekl[0], &order->kekls.kekl[0]);
	ext_to_int_kekl(&ext_kekls->kekl[1], &order->kekls.kekl[1]);
	request->op = TO_KEKL_SET;
	tape_ccw_end(request->cpaddr, PERF_SUBSYS_FUNC, sizeof(*order), order);

	return tape_do_io_free(device, request);
}

/*
 * IOCTL: Set KEKLs
 */
static int tape_3592_ioctl_kekl_set(struct tape_device *device,
				    unsigned long arg)
{
	int rc;
	struct tape390_kekl_pair *ext_kekls;

	DBF_EVENT(6, "tape_3592_ioctl_kekl_set\n");
	if (!crypt_supported(device))
		return -ENOSYS;
	if (!crypt_enabled(device))
		return -EUNATCH;
	ext_kekls = kmalloc(sizeof(*ext_kekls), GFP_KERNEL);
	if (!ext_kekls)
		return -ENOMEM;
	if (copy_from_user(ext_kekls, (char __user *)arg, sizeof(*ext_kekls))) {
		rc = -EFAULT;
		goto out;
	}
	rc = tape_3592_kekl_set(device, ext_kekls);
out:
	kfree(ext_kekls);
	return rc;
}

/*
 * Enable encryption
 */
static int tape_3592_enable_crypt(struct tape_device *device)
{
	struct tape_request *request;
	char *data;

	DBF_EVENT(6, "tape_3592_enable_crypt\n");
	if (!crypt_supported(device))
		return -ENOSYS;
	request = tape_alloc_request(2, 72);
	if (IS_ERR(request))
		return PTR_ERR(request);
	data = request->cpdata;
	memset(data,0,72);

	data[0]       = 0x05;
	data[36 + 0]  = 0x03;
	data[36 + 1]  = 0x03;
	data[36 + 4]  = 0x40;
	data[36 + 6]  = 0x01;
	data[36 + 14] = 0x2f;
	data[36 + 18] = 0xc3;
	data[36 + 35] = 0x72;
	request->op = TO_CRYPT_ON;
	tape_ccw_cc(request->cpaddr, MODE_SET_CB, 36, data);
	tape_ccw_end(request->cpaddr + 1, MODE_SET_CB, 36, data + 36);
	return tape_do_io_free(device, request);
}

/*
 * Disable encryption
 */
static int tape_3592_disable_crypt(struct tape_device *device)
{
	struct tape_request *request;
	char *data;

	DBF_EVENT(6, "tape_3592_disable_crypt\n");
	if (!crypt_supported(device))
		return -ENOSYS;
	request = tape_alloc_request(2, 72);
	if (IS_ERR(request))
		return PTR_ERR(request);
	data = request->cpdata;
	memset(data,0,72);

	data[0]       = 0x05;
	data[36 + 0]  = 0x03;
	data[36 + 1]  = 0x03;
	data[36 + 35] = 0x32;

	request->op = TO_CRYPT_OFF;
	tape_ccw_cc(request->cpaddr, MODE_SET_CB, 36, data);
	tape_ccw_end(request->cpaddr + 1, MODE_SET_CB, 36, data + 36);

	return tape_do_io_free(device, request);
}

/*
 * IOCTL: Set encryption status
 */
static int tape_3592_ioctl_crypt_set(struct tape_device *device,
				     unsigned long arg)
{
	struct tape390_crypt_info info;

	DBF_EVENT(6, "tape_3592_ioctl_crypt_set\n");
	if (!crypt_supported(device))
		return -ENOSYS;
	if (copy_from_user(&info, (char __user *)arg, sizeof(info)))
		return -EFAULT;
	if (info.status & ~TAPE390_CRYPT_ON_MASK)
		return -EINVAL;
	if (info.status & TAPE390_CRYPT_ON_MASK)
		return tape_3592_enable_crypt(device);
	else
		return tape_3592_disable_crypt(device);
}

static int tape_3590_sense_medium(struct tape_device *device);

/*
 * IOCTL: Query enryption status
 */
static int tape_3592_ioctl_crypt_query(struct tape_device *device,
				       unsigned long arg)
{
	DBF_EVENT(6, "tape_3592_ioctl_crypt_query\n");
	if (!crypt_supported(device))
		return -ENOSYS;
	tape_3590_sense_medium(device);
	if (copy_to_user((char __user *) arg, &TAPE_3590_CRYPT_INFO(device),
		sizeof(TAPE_3590_CRYPT_INFO(device))))
		return -EFAULT;
	else
		return 0;
}

/*
 * 3590 IOCTL Overload
 */
static int
tape_3590_ioctl(struct tape_device *device, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case TAPE390_DISPLAY: {
		struct display_struct disp;

		if (copy_from_user(&disp, (char __user *) arg, sizeof(disp)))
			return -EFAULT;

		return tape_std_display(device, &disp);
	}
	case TAPE390_KEKL_SET:
		return tape_3592_ioctl_kekl_set(device, arg);
	case TAPE390_KEKL_QUERY:
		return tape_3592_ioctl_kekl_query(device, arg);
	case TAPE390_CRYPT_SET:
		return tape_3592_ioctl_crypt_set(device, arg);
	case TAPE390_CRYPT_QUERY:
		return tape_3592_ioctl_crypt_query(device, arg);
	default:
		return -EINVAL;	/* no additional ioctls */
	}
}

/*
 * SENSE Medium: Get Sense data about medium state
 */
static int
tape_3590_sense_medium(struct tape_device *device)
{
	struct tape_request *request;

	request = tape_alloc_request(1, 128);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_MSEN;
	tape_ccw_end(request->cpaddr, MEDIUM_SENSE, 128, request->cpdata);
	return tape_do_io_free(device, request);
}

/*
 * MTTELL: Tell block. Return the number of block relative to current file.
 */
static int
tape_3590_mttell(struct tape_device *device, int mt_count)
{
	__u64 block_id;
	int rc;

	rc = tape_std_read_block_id(device, &block_id);
	if (rc)
		return rc;
	return block_id >> 32;
}

/*
 * MTSEEK: seek to the specified block.
 */
static int
tape_3590_mtseek(struct tape_device *device, int count)
{
	struct tape_request *request;

	DBF_EVENT(6, "xsee id: %x\n", count);
	request = tape_alloc_request(3, 4);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_LBL;
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	*(__u32 *) request->cpdata = count;
	tape_ccw_cc(request->cpaddr + 1, LOCATE, 4, request->cpdata);
	tape_ccw_end(request->cpaddr + 2, NOP, 0, NULL);
	return tape_do_io_free(device, request);
}

/*
 * Read Opposite Error Recovery Function:
 * Used, when Read Forward does not work
 */
static void
tape_3590_read_opposite(struct tape_device *device,
			struct tape_request *request)
{
	struct tape_3590_disc_data *data;

	/*
	 * We have allocated 4 ccws in tape_std_read, so we can now
	 * transform the request to a read backward, followed by a
	 * forward space block.
	 */
	request->op = TO_RBA;
	tape_ccw_cc(request->cpaddr, MODE_SET_DB, 1, device->modeset_byte);
	data = device->discdata;
	tape_ccw_cc_idal(request->cpaddr + 1, data->read_back_op,
			 device->char_data.idal_buf);
	tape_ccw_cc(request->cpaddr + 2, FORSPACEBLOCK, 0, NULL);
	tape_ccw_end(request->cpaddr + 3, NOP, 0, NULL);
	DBF_EVENT(6, "xrop ccwg\n");
}

/*
 * Read Attention Msg
 * This should be done after an interrupt with attention bit (0x80)
 * in device state.
 *
 * After a "read attention message" request there are two possible
 * results:
 *
 * 1. A unit check is presented, when attention sense is present (e.g. when
 * a medium has been unloaded). The attention sense comes then
 * together with the unit check. The recovery action is either "retry"
 * (in case there is an attention message pending) or "permanent error".
 *
 * 2. The attention msg is written to the "read subsystem data" buffer.
 * In this case we probably should print it to the console.
 */
static int
tape_3590_read_attmsg(struct tape_device *device)
{
	struct tape_request *request;
	char *buf;

	request = tape_alloc_request(3, 4096);
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_READ_ATTMSG;
	buf = request->cpdata;
	buf[0] = PREP_RD_SS_DATA;
	buf[6] = RD_ATTMSG;	/* read att msg */
	tape_ccw_cc(request->cpaddr, PERFORM_SS_FUNC, 12, buf);
	tape_ccw_cc(request->cpaddr + 1, READ_SS_DATA, 4096 - 12, buf + 12);
	tape_ccw_end(request->cpaddr + 2, NOP, 0, NULL);
	return tape_do_io_free(device, request);
}

/*
 * These functions are used to schedule follow-up actions from within an
 * interrupt context (like unsolicited interrupts).
 */
struct work_handler_data {
	struct tape_device *device;
	enum tape_op        op;
	struct work_struct  work;
};

static void
tape_3590_work_handler(struct work_struct *work)
{
	struct work_handler_data *p =
		container_of(work, struct work_handler_data, work);

	switch (p->op) {
	case TO_MSEN:
		tape_3590_sense_medium(p->device);
		break;
	case TO_READ_ATTMSG:
		tape_3590_read_attmsg(p->device);
		break;
	case TO_CRYPT_ON:
		tape_3592_enable_crypt(p->device);
		break;
	case TO_CRYPT_OFF:
		tape_3592_disable_crypt(p->device);
		break;
	default:
		DBF_EVENT(3, "T3590: work handler undefined for "
			  "operation 0x%02x\n", p->op);
	}
	tape_put_device(p->device);
	kfree(p);
}

static int
tape_3590_schedule_work(struct tape_device *device, enum tape_op op)
{
	struct work_handler_data *p;

	if ((p = kzalloc(sizeof(*p), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	INIT_WORK(&p->work, tape_3590_work_handler);

	p->device = tape_get_device(device);
	p->op = op;

	schedule_work(&p->work);
	return 0;
}

#ifdef CONFIG_S390_TAPE_BLOCK
/*
 * Tape Block READ
 */
static struct tape_request *
tape_3590_bread(struct tape_device *device, struct request *req)
{
	struct tape_request *request;
	struct ccw1 *ccw;
	int count = 0, start_block;
	unsigned off;
	char *dst;
	struct bio_vec *bv;
	struct req_iterator iter;

	DBF_EVENT(6, "xBREDid:");
	start_block = blk_rq_pos(req) >> TAPEBLOCK_HSEC_S2B;
	DBF_EVENT(6, "start_block = %i\n", start_block);

	rq_for_each_segment(bv, req, iter)
		count += bv->bv_len >> (TAPEBLOCK_HSEC_S2B + 9);

	request = tape_alloc_request(2 + count + 1, 4);
	if (IS_ERR(request))
		return request;
	request->op = TO_BLOCK;
	*(__u32 *) request->cpdata = start_block;
	ccw = request->cpaddr;
	ccw = tape_ccw_cc(ccw, MODE_SET_DB, 1, device->modeset_byte);

	/*
	 * We always setup a nop after the mode set ccw. This slot is
	 * used in tape_std_check_locate to insert a locate ccw if the
	 * current tape position doesn't match the start block to be read.
	 */
	ccw = tape_ccw_cc(ccw, NOP, 0, NULL);

	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv->bv_page) + bv->bv_offset;
		for (off = 0; off < bv->bv_len; off += TAPEBLOCK_HSEC_SIZE) {
			ccw->flags = CCW_FLAG_CC;
			ccw->cmd_code = READ_FORWARD;
			ccw->count = TAPEBLOCK_HSEC_SIZE;
			set_normalized_cda(ccw, (void *) __pa(dst));
			ccw++;
			dst += TAPEBLOCK_HSEC_SIZE;
		}
		BUG_ON(off > bv->bv_len);
	}
	ccw = tape_ccw_end(ccw, NOP, 0, NULL);
	DBF_EVENT(6, "xBREDccwg\n");
	return request;
}

static void
tape_3590_free_bread(struct tape_request *request)
{
	struct ccw1 *ccw;

	/* Last ccw is a nop and doesn't need clear_normalized_cda */
	for (ccw = request->cpaddr; ccw->flags & CCW_FLAG_CC; ccw++)
		if (ccw->cmd_code == READ_FORWARD)
			clear_normalized_cda(ccw);
	tape_free_request(request);
}

/*
 * check_locate is called just before the tape request is passed to
 * the common io layer for execution. It has to check the current
 * tape position and insert a locate ccw if it doesn't match the
 * start block for the request.
 */
static void
tape_3590_check_locate(struct tape_device *device, struct tape_request *request)
{
	__u32 *start_block;

	start_block = (__u32 *) request->cpdata;
	if (*start_block != device->blk_data.block_position) {
		/* Add the start offset of the file to get the real block. */
		*start_block += device->bof;
		tape_ccw_cc(request->cpaddr + 1, LOCATE, 4, request->cpdata);
	}
}
#endif

static void tape_3590_med_state_set(struct tape_device *device,
				    struct tape_3590_med_sense *sense)
{
	struct tape390_crypt_info *c_info;

	c_info = &TAPE_3590_CRYPT_INFO(device);

	DBF_EVENT(6, "medium state: %x:%x\n", sense->macst, sense->masst);
	switch (sense->macst) {
	case 0x04:
	case 0x05:
	case 0x06:
		tape_med_state_set(device, MS_UNLOADED);
		TAPE_3590_CRYPT_INFO(device).medium_status = 0;
		return;
	case 0x08:
	case 0x09:
		tape_med_state_set(device, MS_LOADED);
		break;
	default:
		tape_med_state_set(device, MS_UNKNOWN);
		return;
	}
	c_info->medium_status |= TAPE390_MEDIUM_LOADED_MASK;
	if (sense->flags & MSENSE_CRYPT_MASK) {
		DBF_EVENT(6, "Medium is encrypted (%04x)\n", sense->flags);
		c_info->medium_status |= TAPE390_MEDIUM_ENCRYPTED_MASK;
	} else	{
		DBF_EVENT(6, "Medium is not encrypted %04x\n", sense->flags);
		c_info->medium_status &= ~TAPE390_MEDIUM_ENCRYPTED_MASK;
	}
}

/*
 * The done handler is called at device/channel end and wakes up the sleeping
 * process
 */
static int
tape_3590_done(struct tape_device *device, struct tape_request *request)
{
	struct tape_3590_disc_data *disc_data;

	DBF_EVENT(6, "%s done\n", tape_op_verbose[request->op]);
	disc_data = device->discdata;

	switch (request->op) {
	case TO_BSB:
	case TO_BSF:
	case TO_DSE:
	case TO_FSB:
	case TO_FSF:
	case TO_LBL:
	case TO_RFO:
	case TO_RBA:
	case TO_REW:
	case TO_WRI:
	case TO_WTM:
	case TO_BLOCK:
	case TO_LOAD:
		tape_med_state_set(device, MS_LOADED);
		break;
	case TO_RUN:
		tape_med_state_set(device, MS_UNLOADED);
		tape_3590_schedule_work(device, TO_CRYPT_OFF);
		break;
	case TO_MSEN:
		tape_3590_med_state_set(device, request->cpdata);
		break;
	case TO_CRYPT_ON:
		TAPE_3590_CRYPT_INFO(device).status
			|= TAPE390_CRYPT_ON_MASK;
		*(device->modeset_byte) |= 0x03;
		break;
	case TO_CRYPT_OFF:
		TAPE_3590_CRYPT_INFO(device).status
			&= ~TAPE390_CRYPT_ON_MASK;
		*(device->modeset_byte) &= ~0x03;
		break;
	case TO_RBI:	/* RBI seems to succeed even without medium loaded. */
	case TO_NOP:	/* Same to NOP. */
	case TO_READ_CONFIG:
	case TO_READ_ATTMSG:
	case TO_DIS:
	case TO_ASSIGN:
	case TO_UNASSIGN:
	case TO_SIZE:
	case TO_KEKL_SET:
	case TO_KEKL_QUERY:
	case TO_RDC:
		break;
	}
	return TAPE_IO_SUCCESS;
}

/*
 * This fuction is called, when error recovery was successfull
 */
static inline int
tape_3590_erp_succeded(struct tape_device *device, struct tape_request *request)
{
	DBF_EVENT(3, "Error Recovery successful for %s\n",
		  tape_op_verbose[request->op]);
	return tape_3590_done(device, request);
}

/*
 * This fuction is called, when error recovery was not successfull
 */
static inline int
tape_3590_erp_failed(struct tape_device *device, struct tape_request *request,
		     struct irb *irb, int rc)
{
	DBF_EVENT(3, "Error Recovery failed for %s\n",
		  tape_op_verbose[request->op]);
	tape_dump_sense_dbf(device, request, irb);
	return rc;
}

/*
 * Error Recovery do retry
 */
static inline int
tape_3590_erp_retry(struct tape_device *device, struct tape_request *request,
		    struct irb *irb)
{
	DBF_EVENT(2, "Retry: %s\n", tape_op_verbose[request->op]);
	tape_dump_sense_dbf(device, request, irb);
	return TAPE_IO_RETRY;
}

/*
 * Handle unsolicited interrupts
 */
static int
tape_3590_unsolicited_irq(struct tape_device *device, struct irb *irb)
{
	if (irb->scsw.cmd.dstat == DEV_STAT_CHN_END)
		/* Probably result of halt ssch */
		return TAPE_IO_PENDING;
	else if (irb->scsw.cmd.dstat == 0x85)
		/* Device Ready */
		DBF_EVENT(3, "unsol.irq! tape ready: %08x\n", device->cdev_id);
	else if (irb->scsw.cmd.dstat & DEV_STAT_ATTENTION) {
		tape_3590_schedule_work(device, TO_READ_ATTMSG);
	} else {
		DBF_EVENT(3, "unsol.irq! dev end: %08x\n", device->cdev_id);
		tape_dump_sense_dbf(device, NULL, irb);
	}
	/* check medium state */
	tape_3590_schedule_work(device, TO_MSEN);
	return TAPE_IO_SUCCESS;
}

/*
 * Basic Recovery routine
 */
static int
tape_3590_erp_basic(struct tape_device *device, struct tape_request *request,
		    struct irb *irb, int rc)
{
	struct tape_3590_sense *sense;

	sense = (struct tape_3590_sense *) irb->ecw;

	switch (sense->bra) {
	case SENSE_BRA_PER:
		return tape_3590_erp_failed(device, request, irb, rc);
	case SENSE_BRA_CONT:
		return tape_3590_erp_succeded(device, request);
	case SENSE_BRA_RE:
		return tape_3590_erp_retry(device, request, irb);
	case SENSE_BRA_DRE:
		return tape_3590_erp_failed(device, request, irb, rc);
	default:
		BUG();
		return TAPE_IO_STOP;
	}
}

/*
 *  RDL: Read Device (buffered) log
 */
static int
tape_3590_erp_read_buf_log(struct tape_device *device,
			   struct tape_request *request, struct irb *irb)
{
	/*
	 * We just do the basic error recovery at the moment (retry).
	 * Perhaps in the future, we read the log and dump it somewhere...
	 */
	return tape_3590_erp_basic(device, request, irb, -EIO);
}

/*
 *  SWAP: Swap Devices
 */
static int
tape_3590_erp_swap(struct tape_device *device, struct tape_request *request,
		   struct irb *irb)
{
	/*
	 * This error recovery should swap the tapes
	 * if the original has a problem. The operation
	 * should proceed with the new tape... this
	 * should probably be done in user space!
	 */
	dev_warn (&device->cdev->dev, "The tape medium must be loaded into a "
		"different tape unit\n");
	return tape_3590_erp_basic(device, request, irb, -EIO);
}

/*
 *  LBY: Long Busy
 */
static int
tape_3590_erp_long_busy(struct tape_device *device,
			struct tape_request *request, struct irb *irb)
{
	DBF_EVENT(6, "Device is busy\n");
	return TAPE_IO_LONG_BUSY;
}

/*
 *  SPI: Special Intercept
 */
static int
tape_3590_erp_special_interrupt(struct tape_device *device,
				struct tape_request *request, struct irb *irb)
{
	return tape_3590_erp_basic(device, request, irb, -EIO);
}

/*
 *  RDA: Read Alternate
 */
static int
tape_3590_erp_read_alternate(struct tape_device *device,
			     struct tape_request *request, struct irb *irb)
{
	struct tape_3590_disc_data *data;

	/*
	 * The issued Read Backward or Read Previous command is not
	 * supported by the device
	 * The recovery action should be to issue another command:
	 * Read Revious: if Read Backward is not supported
	 * Read Backward: if Read Previous is not supported
	 */
	data = device->discdata;
	if (data->read_back_op == READ_PREVIOUS) {
		DBF_EVENT(2, "(%08x): No support for READ_PREVIOUS command\n",
			  device->cdev_id);
		data->read_back_op = READ_BACKWARD;
	} else {
		DBF_EVENT(2, "(%08x): No support for READ_BACKWARD command\n",
			  device->cdev_id);
		data->read_back_op = READ_PREVIOUS;
	}
	tape_3590_read_opposite(device, request);
	return tape_3590_erp_retry(device, request, irb);
}

/*
 * Error Recovery read opposite
 */
static int
tape_3590_erp_read_opposite(struct tape_device *device,
			    struct tape_request *request, struct irb *irb)
{
	switch (request->op) {
	case TO_RFO:
		/*
		 * We did read forward, but the data could not be read.
		 * We will read backward and then skip forward again.
		 */
		tape_3590_read_opposite(device, request);
		return tape_3590_erp_retry(device, request, irb);
	case TO_RBA:
		/* We tried to read forward and backward, but hat no success */
		return tape_3590_erp_failed(device, request, irb, -EIO);
		break;
	default:
		return tape_3590_erp_failed(device, request, irb, -EIO);
	}
}

/*
 * Print an MIM (Media Information  Message) (message code f0)
 */
static void
tape_3590_print_mim_msg_f0(struct tape_device *device, struct irb *irb)
{
	struct tape_3590_sense *sense;
	char *exception, *service;

	exception = kmalloc(BUFSIZE, GFP_ATOMIC);
	service = kmalloc(BUFSIZE, GFP_ATOMIC);

	if (!exception || !service)
		goto out_nomem;

	sense = (struct tape_3590_sense *) irb->ecw;
	/* Exception Message */
	switch (sense->fmt.f70.emc) {
	case 0x02:
		snprintf(exception, BUFSIZE, "Data degraded");
		break;
	case 0x03:
		snprintf(exception, BUFSIZE, "Data degraded in partion %i",
			sense->fmt.f70.mp);
		break;
	case 0x04:
		snprintf(exception, BUFSIZE, "Medium degraded");
		break;
	case 0x05:
		snprintf(exception, BUFSIZE, "Medium degraded in partition %i",
			sense->fmt.f70.mp);
		break;
	case 0x06:
		snprintf(exception, BUFSIZE, "Block 0 Error");
		break;
	case 0x07:
		snprintf(exception, BUFSIZE, "Medium Exception 0x%02x",
			sense->fmt.f70.md);
		break;
	default:
		snprintf(exception, BUFSIZE, "0x%02x",
			sense->fmt.f70.emc);
		break;
	}
	/* Service Message */
	switch (sense->fmt.f70.smc) {
	case 0x02:
		snprintf(service, BUFSIZE, "Reference Media maintenance "
			"procedure %i", sense->fmt.f70.md);
		break;
	default:
		snprintf(service, BUFSIZE, "0x%02x",
			sense->fmt.f70.smc);
		break;
	}

	dev_warn (&device->cdev->dev, "Tape media information: exception %s, "
		"service %s\n", exception, service);

out_nomem:
	kfree(exception);
	kfree(service);
}

/*
 * Print an I/O Subsystem Service Information Message (message code f1)
 */
static void
tape_3590_print_io_sim_msg_f1(struct tape_device *device, struct irb *irb)
{
	struct tape_3590_sense *sense;
	char *exception, *service;

	exception = kmalloc(BUFSIZE, GFP_ATOMIC);
	service = kmalloc(BUFSIZE, GFP_ATOMIC);

	if (!exception || !service)
		goto out_nomem;

	sense = (struct tape_3590_sense *) irb->ecw;
	/* Exception Message */
	switch (sense->fmt.f71.emc) {
	case 0x01:
		snprintf(exception, BUFSIZE, "Effect of failure is unknown");
		break;
	case 0x02:
		snprintf(exception, BUFSIZE, "CU Exception - no performance "
			"impact");
		break;
	case 0x03:
		snprintf(exception, BUFSIZE, "CU Exception on channel "
			"interface 0x%02x", sense->fmt.f71.md[0]);
		break;
	case 0x04:
		snprintf(exception, BUFSIZE, "CU Exception on device path "
			"0x%02x", sense->fmt.f71.md[0]);
		break;
	case 0x05:
		snprintf(exception, BUFSIZE, "CU Exception on library path "
			"0x%02x", sense->fmt.f71.md[0]);
		break;
	case 0x06:
		snprintf(exception, BUFSIZE, "CU Exception on node 0x%02x",
			sense->fmt.f71.md[0]);
		break;
	case 0x07:
		snprintf(exception, BUFSIZE, "CU Exception on partition "
			"0x%02x", sense->fmt.f71.md[0]);
		break;
	default:
		snprintf(exception, BUFSIZE, "0x%02x",
			sense->fmt.f71.emc);
	}
	/* Service Message */
	switch (sense->fmt.f71.smc) {
	case 0x01:
		snprintf(service, BUFSIZE, "Repair impact is unknown");
		break;
	case 0x02:
		snprintf(service, BUFSIZE, "Repair will not impact cu "
			"performance");
		break;
	case 0x03:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable node "
				"0x%x on CU", sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable "
				"nodes (0x%x-0x%x) on CU", sense->fmt.f71.md[1],
				sense->fmt.f71.md[2]);
		break;
	case 0x04:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable "
				"channel path 0x%x on CU",
				sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable cannel"
				" paths (0x%x-0x%x) on CU",
				sense->fmt.f71.md[1], sense->fmt.f71.md[2]);
		break;
	case 0x05:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable device"
				" path 0x%x on CU", sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable device"
				" paths (0x%x-0x%x) on CU",
				sense->fmt.f71.md[1], sense->fmt.f71.md[2]);
		break;
	case 0x06:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable "
				"library path 0x%x on CU",
				sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable "
				"library paths (0x%x-0x%x) on CU",
				sense->fmt.f71.md[1], sense->fmt.f71.md[2]);
		break;
	case 0x07:
		snprintf(service, BUFSIZE, "Repair will disable access to CU");
		break;
	default:
		snprintf(service, BUFSIZE, "0x%02x",
			sense->fmt.f71.smc);
	}

	dev_warn (&device->cdev->dev, "I/O subsystem information: exception"
		" %s, service %s\n", exception, service);
out_nomem:
	kfree(exception);
	kfree(service);
}

/*
 * Print an Device Subsystem Service Information Message (message code f2)
 */
static void
tape_3590_print_dev_sim_msg_f2(struct tape_device *device, struct irb *irb)
{
	struct tape_3590_sense *sense;
	char *exception, *service;

	exception = kmalloc(BUFSIZE, GFP_ATOMIC);
	service = kmalloc(BUFSIZE, GFP_ATOMIC);

	if (!exception || !service)
		goto out_nomem;

	sense = (struct tape_3590_sense *) irb->ecw;
	/* Exception Message */
	switch (sense->fmt.f71.emc) {
	case 0x01:
		snprintf(exception, BUFSIZE, "Effect of failure is unknown");
		break;
	case 0x02:
		snprintf(exception, BUFSIZE, "DV Exception - no performance"
			" impact");
		break;
	case 0x03:
		snprintf(exception, BUFSIZE, "DV Exception on channel "
			"interface 0x%02x", sense->fmt.f71.md[0]);
		break;
	case 0x04:
		snprintf(exception, BUFSIZE, "DV Exception on loader 0x%02x",
			sense->fmt.f71.md[0]);
		break;
	case 0x05:
		snprintf(exception, BUFSIZE, "DV Exception on message display"
			" 0x%02x", sense->fmt.f71.md[0]);
		break;
	case 0x06:
		snprintf(exception, BUFSIZE, "DV Exception in tape path");
		break;
	case 0x07:
		snprintf(exception, BUFSIZE, "DV Exception in drive");
		break;
	default:
		snprintf(exception, BUFSIZE, "0x%02x",
			sense->fmt.f71.emc);
	}
	/* Service Message */
	switch (sense->fmt.f71.smc) {
	case 0x01:
		snprintf(service, BUFSIZE, "Repair impact is unknown");
		break;
	case 0x02:
		snprintf(service, BUFSIZE, "Repair will not impact device "
			"performance");
		break;
	case 0x03:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable "
				"channel path 0x%x on DV",
				sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable "
				"channel path (0x%x-0x%x) on DV",
				sense->fmt.f71.md[1], sense->fmt.f71.md[2]);
		break;
	case 0x04:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable "
				"interface 0x%x on DV", sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable "
				"interfaces (0x%x-0x%x) on DV",
				sense->fmt.f71.md[1], sense->fmt.f71.md[2]);
		break;
	case 0x05:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable loader"
				" 0x%x on DV", sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable loader"
				" (0x%x-0x%x) on DV",
				sense->fmt.f71.md[1], sense->fmt.f71.md[2]);
		break;
	case 0x07:
		snprintf(service, BUFSIZE, "Repair will disable access to DV");
		break;
	case 0x08:
		if (sense->fmt.f71.mdf == 0)
			snprintf(service, BUFSIZE, "Repair will disable "
				"message display 0x%x on DV",
				sense->fmt.f71.md[1]);
		else
			snprintf(service, BUFSIZE, "Repair will disable "
				"message displays (0x%x-0x%x) on DV",
				 sense->fmt.f71.md[1], sense->fmt.f71.md[2]);
		break;
	case 0x09:
		snprintf(service, BUFSIZE, "Clean DV");
		break;
	default:
		snprintf(service, BUFSIZE, "0x%02x",
			sense->fmt.f71.smc);
	}

	dev_warn (&device->cdev->dev, "Device subsystem information: exception"
		" %s, service %s\n", exception, service);
out_nomem:
	kfree(exception);
	kfree(service);
}

/*
 * Print standard ERA Message
 */
static void
tape_3590_print_era_msg(struct tape_device *device, struct irb *irb)
{
	struct tape_3590_sense *sense;

	sense = (struct tape_3590_sense *) irb->ecw;
	if (sense->mc == 0)
		return;
	if ((sense->mc > 0) && (sense->mc < TAPE_3590_MAX_MSG)) {
		if (tape_3590_msg[sense->mc] != NULL)
			dev_warn (&device->cdev->dev, "The tape unit has "
				"issued sense message %s\n",
				tape_3590_msg[sense->mc]);
		else
			dev_warn (&device->cdev->dev, "The tape unit has "
				"issued an unknown sense message code 0x%x\n",
				sense->mc);
		return;
	}
	if (sense->mc == 0xf0) {
		/* Standard Media Information Message */
		dev_warn (&device->cdev->dev, "MIM SEV=%i, MC=%02x, ES=%x/%x, "
			"RC=%02x-%04x-%02x\n", sense->fmt.f70.sev, sense->mc,
			sense->fmt.f70.emc, sense->fmt.f70.smc,
			sense->fmt.f70.refcode, sense->fmt.f70.mid,
			sense->fmt.f70.fid);
		tape_3590_print_mim_msg_f0(device, irb);
		return;
	}
	if (sense->mc == 0xf1) {
		/* Standard I/O Subsystem Service Information Message */
		dev_warn (&device->cdev->dev, "IOSIM SEV=%i, DEVTYPE=3590/%02x,"
			" MC=%02x, ES=%x/%x, REF=0x%04x-0x%04x-0x%04x\n",
			sense->fmt.f71.sev, device->cdev->id.dev_model,
			sense->mc, sense->fmt.f71.emc, sense->fmt.f71.smc,
			sense->fmt.f71.refcode1, sense->fmt.f71.refcode2,
			sense->fmt.f71.refcode3);
		tape_3590_print_io_sim_msg_f1(device, irb);
		return;
	}
	if (sense->mc == 0xf2) {
		/* Standard Device Service Information Message */
		dev_warn (&device->cdev->dev, "DEVSIM SEV=%i, DEVTYPE=3590/%02x"
			", MC=%02x, ES=%x/%x, REF=0x%04x-0x%04x-0x%04x\n",
			sense->fmt.f71.sev, device->cdev->id.dev_model,
			sense->mc, sense->fmt.f71.emc, sense->fmt.f71.smc,
			sense->fmt.f71.refcode1, sense->fmt.f71.refcode2,
			sense->fmt.f71.refcode3);
		tape_3590_print_dev_sim_msg_f2(device, irb);
		return;
	}
	if (sense->mc == 0xf3) {
		/* Standard Library Service Information Message */
		return;
	}
	dev_warn (&device->cdev->dev, "The tape unit has issued an unknown "
		"sense message code %x\n", sense->mc);
}

static int tape_3590_crypt_error(struct tape_device *device,
				 struct tape_request *request, struct irb *irb)
{
	u8 cu_rc, ekm_rc1;
	u16 ekm_rc2;
	u32 drv_rc;
	const char *bus_id;
	char *sense;

	sense = ((struct tape_3590_sense *) irb->ecw)->fmt.data;
	bus_id = dev_name(&device->cdev->dev);
	cu_rc = sense[0];
	drv_rc = *((u32*) &sense[5]) & 0xffffff;
	ekm_rc1 = sense[9];
	ekm_rc2 = *((u16*) &sense[10]);
	if ((cu_rc == 0) && (ekm_rc2 == 0xee31))
		/* key not defined on EKM */
		return tape_3590_erp_basic(device, request, irb, -EKEYREJECTED);
	if ((cu_rc == 1) || (cu_rc == 2))
		/* No connection to EKM */
		return tape_3590_erp_basic(device, request, irb, -ENOTCONN);

	dev_err (&device->cdev->dev, "The tape unit failed to obtain the "
		"encryption key from EKM\n");

	return tape_3590_erp_basic(device, request, irb, -ENOKEY);
}

/*
 *  3590 error Recovery routine:
 *  If possible, it tries to recover from the error. If this is not possible,
 *  inform the user about the problem.
 */
static int
tape_3590_unit_check(struct tape_device *device, struct tape_request *request,
		     struct irb *irb)
{
	struct tape_3590_sense *sense;
	int rc;

#ifdef CONFIG_S390_TAPE_BLOCK
	if (request->op == TO_BLOCK) {
		/*
		 * Recovery for block device requests. Set the block_position
		 * to something invalid and retry.
		 */
		device->blk_data.block_position = -1;
		if (request->retries-- <= 0)
			return tape_3590_erp_failed(device, request, irb, -EIO);
		else
			return tape_3590_erp_retry(device, request, irb);
	}
#endif

	sense = (struct tape_3590_sense *) irb->ecw;

	DBF_EVENT(6, "Unit Check: RQC = %x\n", sense->rc_rqc);

	/*
	 * First check all RC-QRCs where we want to do something special
	 *   - "break":     basic error recovery is done
	 *   - "goto out:": just print error message if available
	 */
	rc = -EIO;
	switch (sense->rc_rqc) {

	case 0x1110:
		tape_3590_print_era_msg(device, irb);
		return tape_3590_erp_read_buf_log(device, request, irb);

	case 0x2011:
		tape_3590_print_era_msg(device, irb);
		return tape_3590_erp_read_alternate(device, request, irb);

	case 0x2230:
	case 0x2231:
		tape_3590_print_era_msg(device, irb);
		return tape_3590_erp_special_interrupt(device, request, irb);
	case 0x2240:
		return tape_3590_crypt_error(device, request, irb);

	case 0x3010:
		DBF_EVENT(2, "(%08x): Backward at Beginning of Partition\n",
			  device->cdev_id);
		return tape_3590_erp_basic(device, request, irb, -ENOSPC);
	case 0x3012:
		DBF_EVENT(2, "(%08x): Forward at End of Partition\n",
			  device->cdev_id);
		return tape_3590_erp_basic(device, request, irb, -ENOSPC);
	case 0x3020:
		DBF_EVENT(2, "(%08x): End of Data Mark\n", device->cdev_id);
		return tape_3590_erp_basic(device, request, irb, -ENOSPC);

	case 0x3122:
		DBF_EVENT(2, "(%08x): Rewind Unload initiated\n",
			  device->cdev_id);
		return tape_3590_erp_basic(device, request, irb, -EIO);
	case 0x3123:
		DBF_EVENT(2, "(%08x): Rewind Unload complete\n",
			  device->cdev_id);
		tape_med_state_set(device, MS_UNLOADED);
		tape_3590_schedule_work(device, TO_CRYPT_OFF);
		return tape_3590_erp_basic(device, request, irb, 0);

	case 0x4010:
		/*
		 * print additional msg since default msg
		 * "device intervention" is not very meaningfull
		 */
		tape_med_state_set(device, MS_UNLOADED);
		tape_3590_schedule_work(device, TO_CRYPT_OFF);
		return tape_3590_erp_basic(device, request, irb, -ENOMEDIUM);
	case 0x4012:		/* Device Long Busy */
		/* XXX: Also use long busy handling here? */
		DBF_EVENT(6, "(%08x): LONG BUSY\n", device->cdev_id);
		tape_3590_print_era_msg(device, irb);
		return tape_3590_erp_basic(device, request, irb, -EBUSY);
	case 0x4014:
		DBF_EVENT(6, "(%08x): Crypto LONG BUSY\n", device->cdev_id);
		return tape_3590_erp_long_busy(device, request, irb);

	case 0x5010:
		if (sense->rac == 0xd0) {
			/* Swap */
			tape_3590_print_era_msg(device, irb);
			return tape_3590_erp_swap(device, request, irb);
		}
		if (sense->rac == 0x26) {
			/* Read Opposite */
			tape_3590_print_era_msg(device, irb);
			return tape_3590_erp_read_opposite(device, request,
							   irb);
		}
		return tape_3590_erp_basic(device, request, irb, -EIO);
	case 0x5020:
	case 0x5021:
	case 0x5022:
	case 0x5040:
	case 0x5041:
	case 0x5042:
		tape_3590_print_era_msg(device, irb);
		return tape_3590_erp_swap(device, request, irb);

	case 0x5110:
	case 0x5111:
		return tape_3590_erp_basic(device, request, irb, -EMEDIUMTYPE);

	case 0x5120:
	case 0x1120:
		tape_med_state_set(device, MS_UNLOADED);
		tape_3590_schedule_work(device, TO_CRYPT_OFF);
		return tape_3590_erp_basic(device, request, irb, -ENOMEDIUM);

	case 0x6020:
		return tape_3590_erp_basic(device, request, irb, -EMEDIUMTYPE);

	case 0x8011:
		return tape_3590_erp_basic(device, request, irb, -EPERM);
	case 0x8013:
		dev_warn (&device->cdev->dev, "A different host has privileged"
			" access to the tape unit\n");
		return tape_3590_erp_basic(device, request, irb, -EPERM);
	default:
		return tape_3590_erp_basic(device, request, irb, -EIO);
	}
}

/*
 * 3590 interrupt handler:
 */
static int
tape_3590_irq(struct tape_device *device, struct tape_request *request,
	      struct irb *irb)
{
	if (request == NULL)
		return tape_3590_unsolicited_irq(device, irb);

	if ((irb->scsw.cmd.dstat & DEV_STAT_UNIT_EXCEP) &&
	    (irb->scsw.cmd.dstat & DEV_STAT_DEV_END) &&
	    (request->op == TO_WRI)) {
		/* Write at end of volume */
		DBF_EVENT(2, "End of volume\n");
		return tape_3590_erp_failed(device, request, irb, -ENOSPC);
	}

	if (irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK)
		return tape_3590_unit_check(device, request, irb);

	if (irb->scsw.cmd.dstat & DEV_STAT_DEV_END) {
		if (irb->scsw.cmd.dstat == DEV_STAT_UNIT_EXCEP) {
			if (request->op == TO_FSB || request->op == TO_BSB)
				request->rescnt++;
			else
				DBF_EVENT(5, "Unit Exception!\n");
		}

		return tape_3590_done(device, request);
	}

	if (irb->scsw.cmd.dstat & DEV_STAT_CHN_END) {
		DBF_EVENT(2, "cannel end\n");
		return TAPE_IO_PENDING;
	}

	if (irb->scsw.cmd.dstat & DEV_STAT_ATTENTION) {
		DBF_EVENT(2, "Unit Attention when busy..\n");
		return TAPE_IO_PENDING;
	}

	DBF_EVENT(6, "xunknownirq\n");
	tape_dump_sense_dbf(device, request, irb);
	return TAPE_IO_STOP;
}


static int tape_3590_read_dev_chars(struct tape_device *device,
				    struct tape_3590_rdc_data *rdc_data)
{
	int rc;
	struct tape_request *request;

	request = tape_alloc_request(1, sizeof(*rdc_data));
	if (IS_ERR(request))
		return PTR_ERR(request);
	request->op = TO_RDC;
	tape_ccw_end(request->cpaddr, CCW_CMD_RDC, sizeof(*rdc_data),
		     request->cpdata);
	rc = tape_do_io(device, request);
	if (rc == 0)
		memcpy(rdc_data, request->cpdata, sizeof(*rdc_data));
	tape_free_request(request);
	return rc;
}

/*
 * Setup device function
 */
static int
tape_3590_setup_device(struct tape_device *device)
{
	int rc;
	struct tape_3590_disc_data *data;
	struct tape_3590_rdc_data *rdc_data;

	DBF_EVENT(6, "3590 device setup\n");
	data = kzalloc(sizeof(struct tape_3590_disc_data), GFP_KERNEL | GFP_DMA);
	if (data == NULL)
		return -ENOMEM;
	data->read_back_op = READ_PREVIOUS;
	device->discdata = data;

	rdc_data = kmalloc(sizeof(*rdc_data), GFP_KERNEL | GFP_DMA);
	if (!rdc_data) {
		rc = -ENOMEM;
		goto fail_kmalloc;
	}
	rc = tape_3590_read_dev_chars(device, rdc_data);
	if (rc) {
		DBF_LH(3, "Read device characteristics failed!\n");
		goto fail_rdc_data;
	}
	rc = tape_std_assign(device);
	if (rc)
		goto fail_rdc_data;
	if (rdc_data->data[31] == 0x13) {
		data->crypt_info.capability |= TAPE390_CRYPT_SUPPORTED_MASK;
		tape_3592_disable_crypt(device);
	} else {
		DBF_EVENT(6, "Device has NO crypto support\n");
	}
	/* Try to find out if medium is loaded */
	rc = tape_3590_sense_medium(device);
	if (rc) {
		DBF_LH(3, "3590 medium sense returned %d\n", rc);
		goto fail_rdc_data;
	}
	return 0;

fail_rdc_data:
	kfree(rdc_data);
fail_kmalloc:
	kfree(data);
	return rc;
}

/*
 * Cleanup device function
 */
static void
tape_3590_cleanup_device(struct tape_device *device)
{
	flush_scheduled_work();
	tape_std_unassign(device);

	kfree(device->discdata);
	device->discdata = NULL;
}

/*
 * List of 3590 magnetic tape commands.
 */
static tape_mtop_fn tape_3590_mtop[TAPE_NR_MTOPS] = {
	[MTRESET]	 = tape_std_mtreset,
	[MTFSF]		 = tape_std_mtfsf,
	[MTBSF]		 = tape_std_mtbsf,
	[MTFSR]		 = tape_std_mtfsr,
	[MTBSR]		 = tape_std_mtbsr,
	[MTWEOF]	 = tape_std_mtweof,
	[MTREW]		 = tape_std_mtrew,
	[MTOFFL]	 = tape_std_mtoffl,
	[MTNOP]		 = tape_std_mtnop,
	[MTRETEN]	 = tape_std_mtreten,
	[MTBSFM]	 = tape_std_mtbsfm,
	[MTFSFM]	 = tape_std_mtfsfm,
	[MTEOM]		 = tape_std_mteom,
	[MTERASE]	 = tape_std_mterase,
	[MTRAS1]	 = NULL,
	[MTRAS2]	 = NULL,
	[MTRAS3]	 = NULL,
	[MTSETBLK]	 = tape_std_mtsetblk,
	[MTSETDENSITY]	 = NULL,
	[MTSEEK]	 = tape_3590_mtseek,
	[MTTELL]	 = tape_3590_mttell,
	[MTSETDRVBUFFER] = NULL,
	[MTFSS]		 = NULL,
	[MTBSS]		 = NULL,
	[MTWSM]		 = NULL,
	[MTLOCK]	 = NULL,
	[MTUNLOCK]	 = NULL,
	[MTLOAD]	 = tape_std_mtload,
	[MTUNLOAD]	 = tape_std_mtunload,
	[MTCOMPRESSION]	 = tape_std_mtcompression,
	[MTSETPART]	 = NULL,
	[MTMKPART]	 = NULL
};

/*
 * Tape discipline structure for 3590.
 */
static struct tape_discipline tape_discipline_3590 = {
	.owner = THIS_MODULE,
	.setup_device = tape_3590_setup_device,
	.cleanup_device = tape_3590_cleanup_device,
	.process_eov = tape_std_process_eov,
	.irq = tape_3590_irq,
	.read_block = tape_std_read_block,
	.write_block = tape_std_write_block,
#ifdef CONFIG_S390_TAPE_BLOCK
	.bread = tape_3590_bread,
	.free_bread = tape_3590_free_bread,
	.check_locate = tape_3590_check_locate,
#endif
	.ioctl_fn = tape_3590_ioctl,
	.mtop_array = tape_3590_mtop
};

static struct ccw_device_id tape_3590_ids[] = {
	{CCW_DEVICE_DEVTYPE(0x3590, 0, 0x3590, 0), .driver_info = tape_3590},
	{CCW_DEVICE_DEVTYPE(0x3592, 0, 0x3592, 0), .driver_info = tape_3592},
	{ /* end of list */ }
};

static int
tape_3590_online(struct ccw_device *cdev)
{
	return tape_generic_online(dev_get_drvdata(&cdev->dev),
				   &tape_discipline_3590);
}

static struct ccw_driver tape_3590_driver = {
	.name = "tape_3590",
	.owner = THIS_MODULE,
	.ids = tape_3590_ids,
	.probe = tape_generic_probe,
	.remove = tape_generic_remove,
	.set_offline = tape_generic_offline,
	.set_online = tape_3590_online,
	.freeze = tape_generic_pm_suspend,
};

/*
 * Setup discipline structure.
 */
static int
tape_3590_init(void)
{
	int rc;

	TAPE_DBF_AREA = debug_register("tape_3590", 2, 2, 4 * sizeof(long));
	debug_register_view(TAPE_DBF_AREA, &debug_sprintf_view);
#ifdef DBF_LIKE_HELL
	debug_set_level(TAPE_DBF_AREA, 6);
#endif

	DBF_EVENT(3, "3590 init\n");
	/* Register driver for 3590 tapes. */
	rc = ccw_driver_register(&tape_3590_driver);
	if (rc)
		DBF_EVENT(3, "3590 init failed\n");
	else
		DBF_EVENT(3, "3590 registered\n");
	return rc;
}

static void
tape_3590_exit(void)
{
	ccw_driver_unregister(&tape_3590_driver);

	debug_unregister(TAPE_DBF_AREA);
}

MODULE_DEVICE_TABLE(ccw, tape_3590_ids);
MODULE_AUTHOR("(C) 2001,2006 IBM Corporation");
MODULE_DESCRIPTION("Linux on zSeries channel attached 3590 tape device driver");
MODULE_LICENSE("GPL");

module_init(tape_3590_init);
module_exit(tape_3590_exit);

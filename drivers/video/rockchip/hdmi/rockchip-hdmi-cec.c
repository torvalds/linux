#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include "rockchip-hdmi-cec.h"

static struct cec_device *cec_dev;
struct input_dev *devinput;
static struct miscdevice mdev;

int key_table[] = {
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_REPLY,
	KEY_BACK,
	KEY_POWER,
};

static void cecmenucontrol(int uitemp);

static int cecreadframe(struct cec_framedata *frame)
{
	if (frame == NULL || !cec_dev || cec_dev->readframe == NULL)
		return -1;
	else
		return cec_dev->readframe(cec_dev->hdmi, frame);
}

static int cecsendframe(struct cec_framedata *frame)
{
	if (frame == NULL || !cec_dev || cec_dev->readframe == NULL)
		return -1;
	else
		return cec_dev->sendframe(cec_dev->hdmi, frame);
}

static int cecsendping(char logicaddress)
{
	struct cec_framedata cecframe;

	memset(&cecframe, 0, sizeof(struct cec_framedata));
	cecframe.srcdestaddr = logicaddress << 4 | logicaddress;
	return cec_dev->sendframe(cec_dev->hdmi, &cecframe);
}

/*static int CecSendMessage (char opCode, char dest)
{
	struct cec_framedata cecframe;

	cecframe.opcode        = opCode;
	cecframe.srcdestaddr   = MAKE_SRCDEST(cec_dev->address_logic, dest);
	cecframe.argcount      = 0;

	return cecsendframe(&cecframe);
}*/


/*static void CecSendFeatureAbort (struct cec_framedata *pcpi, char reason)
{
	struct cec_framedata cecframe;

	if ((pcpi->srcdestaddr & 0x0F) != CEC_LOGADDR_UNREGORBC) {
		cecframe.opcode        = CECOP_FEATURE_ABORT;
		cecframe.srcdestaddr   = MAKE_SRCDEST( cec_dev->address_logic,
					( pcpi->srcdestaddr & 0xF0) >> 4 );
		cecframe.args[0]       = pcpi->opcode;
		cecframe.args[1]       = reason;
		cecframe.argcount      = 2;
		cecsendframe(&cecframe);
	}
}*/

static void cecsendimageview(void)
{
	 struct cec_framedata cecframe;

	 cecframe.opcode	= CECOP_IMAGE_VIEW_ON;
	 cecframe.srcdestaddr	= MAKE_SRCDEST(cec_dev->address_logic,
					       CEC_LOGADDR_UNREGORBC);
	 cecframe.argcount	= 0;
	 cecsendframe(&cecframe);
}

static void cecsendactivesource(void)
{
	struct cec_framedata cecframe;

	cecframe.opcode        = CECOP_ACTIVE_SOURCE;
	cecframe.srcdestaddr   = MAKE_SRCDEST(cec_dev->address_logic,
					      CEC_LOGADDR_UNREGORBC);
	cecframe.args[0]       = (cec_dev->address_phy & 0xFF00) >> 8;
	cecframe.args[1]       = (cec_dev->address_phy & 0x00FF);
	cecframe.argcount      = 2;
	cecsendframe(&cecframe);
}

static void cechandleinactivesource(struct cec_framedata *pcpi)
{
}

static void cechandlefeatureabort(struct cec_framedata *pcpi)
{
}

static bool validatececmessage(struct cec_framedata *pcpi)
{
	char parametercount = 0;
	bool    countok = true;

	/* Determine required parameter count   */

	switch (pcpi->opcode) {
	case CECOP_IMAGE_VIEW_ON:
	case CECOP_TEXT_VIEW_ON:
	case CECOP_STANDBY:
	case CECOP_GIVE_PHYSICAL_ADDRESS:
	case CECOP_GIVE_DEVICE_POWER_STATUS:
	case CECOP_GET_MENU_LANGUAGE:
	case CECOP_GET_CEC_VERSION:
		parametercount = 0;
		break;
	case CECOP_REPORT_POWER_STATUS:         /* power status*/
	case CECOP_CEC_VERSION:                 /* cec version*/
		parametercount = 1;
		break;
	case CECOP_INACTIVE_SOURCE:             /* physical address*/
	case CECOP_FEATURE_ABORT:
	case CECOP_ACTIVE_SOURCE:               /* physical address*/
		parametercount = 2;
		break;
	case CECOP_REPORT_PHYSICAL_ADDRESS:
	case CECOP_DEVICE_VENDOR_ID:            /* vendor id*/
		parametercount = 3;
		break;
	case CECOP_SET_OSD_NAME:                /* osd name (1-14 bytes)*/
	case CECOP_SET_OSD_STRING:
		parametercount = 1;    /* must have a minimum of 1 operands*/
		break;
	case CECOP_ABORT:
		break;
	case CECOP_ARC_INITIATE:
		break;
	case CECOP_ARC_REPORT_INITIATED:
		break;
	case CECOP_ARC_REPORT_TERMINATED:
		break;
	case CECOP_ARC_REQUEST_INITIATION:
		break;
	case CECOP_ARC_REQUEST_TERMINATION:
		break;
	case CECOP_ARC_TERMINATE:
		break;
	default:
		break;
	}

	/* Test for correct parameter count.    */

	if (pcpi->argcount < parametercount)
		countok = false;

	return countok;
}

static bool cecrxmsghandlerlast(struct cec_framedata *pcpi)
{
	bool			isdirectaddressed;
	struct cec_framedata	cecframe;

	isdirectaddressed = !((pcpi->srcdestaddr & 0x0F) ==
			      CEC_LOGADDR_UNREGORBC);
	pr_info("isDirectAddressed %d\n", (int)isdirectaddressed);
	if (validatececmessage(pcpi)) {
		/* If invalid message, ignore it, but treat it as handled */
	if (isdirectaddressed) {
		switch (pcpi->opcode) {
		case CECOP_USER_CONTROL_PRESSED:
			cecmenucontrol(pcpi->args[0]);
			break;

		case CECOP_VENDOR_REMOTE_BUTTON_DOWN:
			cecmenucontrol(pcpi->args[0]);
			break;
		case CECOP_FEATURE_ABORT:
			cechandlefeatureabort(pcpi);
			break;

		case CECOP_GIVE_OSD_NAME:
			cecframe.opcode        = CECOP_SET_OSD_NAME;
			cecframe.srcdestaddr =
				MAKE_SRCDEST(cec_dev->address_logic,
					     CEC_LOGADDR_TV);
			cecframe.args[0]  = 'r';
			cecframe.args[1]  = 'k';
			cecframe.args[2]  = '-';
			cecframe.args[3]  = 'b';
			cecframe.args[4]  = 'o';
			cecframe.args[5]  = 'x';
			cecframe.argcount      = 6;
			cecsendframe(&cecframe);
			break;

		case CECOP_VENDOR_COMMAND_WITH_ID:

		if (pcpi->args[2] == 00) {
			cecframe.opcode        = CECOP_SET_OSD_NAME;
			cecframe.srcdestaddr =
				MAKE_SRCDEST(cec_dev->address_logic,
					     CEC_LOGADDR_TV);
			cecframe.args[0]  = '1';
			cecframe.args[1]  = '1';
			cecframe.args[2]  = '1';
			cecframe.args[3]  = '1';
			cecframe.args[4]  = '1';
			cecframe.args[5]  = '1';
			cecframe.argcount      = 6;
			cecsendframe(&cecframe);
			}
			break;
		case CECOP_IMAGE_VIEW_ON:
		case CECOP_TEXT_VIEW_ON:
		/* In our case, respond the same to both these messages*/
		    break;

		case CECOP_GIVE_DEVICE_VENDOR_ID:
			cecframe.opcode        = CECOP_DEVICE_VENDOR_ID;
			cecframe.srcdestaddr   =
				MAKE_SRCDEST(cec_dev->address_logic,
					     CEC_LOGADDR_UNREGORBC);
			cecframe.args[0]       = 0x1;
			cecframe.args[1]       = 0x2;
			cecframe.args[2]       = 0x3;
			cecframe.argcount      = 3;
			cecsendframe(&cecframe);
			break;

		case CECOP_STANDBY:             /* Direct and Broadcast*/
		/* Setting this here will let the main task know    */
		/* (via SI_CecGetPowerState) and at the same time   */
		/* prevent us from broadcasting a STANDBY message   */
		/* of our own when the main task responds by        */
		/* calling SI_CecSetPowerState( STANDBY );          */
			cec_dev->powerstatus = CEC_POWERSTATUS_STANDBY;
			break;

		case CECOP_INACTIVE_SOURCE:
			cechandleinactivesource(pcpi);
			break;

		case CECOP_GIVE_PHYSICAL_ADDRESS:

			cecframe.opcode        = CECOP_REPORT_PHYSICAL_ADDRESS;
			cecframe.srcdestaddr   =
				MAKE_SRCDEST(cec_dev->address_logic,
					     CEC_LOGADDR_UNREGORBC);
			cecframe.args[0]   = (cec_dev->address_phy&0xFF00)>>8;
			cecframe.args[1]       = (cec_dev->address_phy&0x00FF);
			cecframe.args[2]       = cec_dev->address_logic;
			cecframe.argcount      = 3;
			cecsendframe(&cecframe);
			break;

		case CECOP_GIVE_DEVICE_POWER_STATUS:
		/* TV responds with power status.   */

			cecframe.opcode        = CECOP_REPORT_POWER_STATUS;
			cecframe.srcdestaddr   =
				MAKE_SRCDEST(cec_dev->address_logic,
					     (pcpi->srcdestaddr & 0xF0) >> 4);
			cec_dev->powerstatus =  0x00;
			cecframe.args[0]       = cec_dev->powerstatus;
			cecframe.argcount      = 1;
			cecsendframe(&cecframe);
			break;

		case CECOP_GET_MENU_LANGUAGE:
		/* TV Responds with a Set Menu language command.    */

			cecframe.opcode         = CECOP_SET_MENU_LANGUAGE;
			cecframe.srcdestaddr    =
				MAKE_SRCDEST(cec_dev->address_logic,
					     CEC_LOGADDR_UNREGORBC);
			cecframe.args[0]        = 'e';
			cecframe.args[1]        = 'n';
			cecframe.args[2]        = 'g';
			cecframe.argcount       = 3;
			cecsendframe(&cecframe);
			break;

		case CECOP_GET_CEC_VERSION:
		/* TV responds to this request with it's CEC version support.*/

			cecframe.srcdestaddr   =
				MAKE_SRCDEST(cec_dev->address_logic,
					     CEC_LOGADDR_TV);
			cecframe.opcode        = CECOP_CEC_VERSION;
			cecframe.args[0]       = 0x05;       /* Report CEC1.4b*/
			cecframe.argcount      = 1;
			cecsendframe(&cecframe);
			break;

		case CECOP_REPORT_POWER_STATUS:
		/*Someone sent us their power state.

			l_sourcePowerStatus = pcpi->args[0];

			let NEW SOURCE task know about it.

			if ( l_cecTaskState.task == SI_CECTASK_NEWSOURCE )
			{
			l_cecTaskState.cpiState = CPI_RESPONSE;
			}*/
			 break;

		/* Do not reply to directly addressed 'Broadcast' msgs.  */
		case CECOP_REQUEST_ACTIVE_SOURCE:
			cecsendactivesource();
			break;

		case CECOP_ACTIVE_SOURCE:
		case CECOP_REPORT_PHYSICAL_ADDRESS:
		case CECOP_ROUTING_CHANGE:
		case CECOP_ROUTING_INFORMATION:
		case CECOP_SET_STREAM_PATH:
		case CECOP_SET_MENU_LANGUAGE:
		case CECOP_DEVICE_VENDOR_ID:
			break;

		case CECOP_ABORT:
			break;
		default:
		/*CecSendFeatureAbort(pcpi, CECAR_UNRECOG_OPCODE);*/
			break;
			}
		} else {
			/* Respond to broadcast messages.   */
			switch (pcpi->opcode) {
			case CECOP_STANDBY:
			/* Setting this here will let the main task know    */
			/* (via SI_CecGetPowerState) and at the same time   */
			/* prevent us from broadcasting a STANDBY message   */
			/* of our own when the main task responds by        */
			/* calling SI_CecSetPowerState( STANDBY );          */
				cec_dev->powerstatus = CEC_POWERSTATUS_STANDBY;
				input_event(devinput, EV_KEY, KEY_POWER, 1);
				input_sync(devinput);
				input_event(devinput, EV_KEY, KEY_POWER, 0);
				input_sync(devinput);
				break;

			case CECOP_ACTIVE_SOURCE:
				/*CecHandleActiveSource( pcpi );*/
				break;

			case CECOP_REPORT_PHYSICAL_ADDRESS:
				/*CecHandleReportPhysicalAddress( pcpi );*/
				cecframe.srcdestaddr   =
					MAKE_SRCDEST(cec_dev->address_logic,
						     CEC_LOGADDR_UNREGORBC);
				cecframe.opcode        = CECOP_CEC_VERSION;
				cecframe.args[0]       = 0x05; /* CEC1.4b*/
				cecframe.argcount      = 1;
				cecsendframe(&cecframe);
				break;

		/* Do not reply to 'Broadcast' msgs that we don't need.*/
			case CECOP_REQUEST_ACTIVE_SOURCE:
				cecsendactivesource();
				break;
			case CECOP_ROUTING_CHANGE:
			case CECOP_ROUTING_INFORMATION:
			case CECOP_SET_STREAM_PATH:
			case CECOP_SET_MENU_LANGUAGE:
				break;
			}
		}
	}

	return 0;
}

static void cecenumeration(void)
{
	char logicaddress[3] = {CEC_LOGADDR_PLAYBACK1,
				CEC_LOGADDR_PLAYBACK2,
				CEC_LOGADDR_PLAYBACK3};
	int i;

	if (!cec_dev)
		return;

	for (i = 0; i < 3; i++) {
		if (cecsendping(logicaddress[i])) {
			cec_dev->address_logic = logicaddress[i];
			CECDBG("Logic Address is 0x%x\n",
			       cec_dev->address_logic);
			break;
		}
	}
	if (i == 3)
		cec_dev->address_logic = CEC_LOGADDR_UNREGORBC;
	cec_dev->setceclogicaddr(cec_dev->hdmi, cec_dev->address_logic);
	cecsendimageview();
	cecsendactivesource();
}

static void cecworkfunc(struct work_struct *work)
{
	struct cec_delayed_work *cec_w =
		container_of(work, struct cec_delayed_work, work.work);
	struct cec_framedata cecframe;

	switch (cec_w->event) {
	case EVENT_ENUMERATE:
		cecenumeration();
		break;
	case EVENT_RX_FRAME:
		memset(&cecframe, 0, sizeof(struct cec_framedata));
		cecreadframe(&cecframe);
		cecrxmsghandlerlast(&cecframe);
		break;
	default:
		break;
	}

	kfree(cec_w->data);
	kfree(cec_w);
}

void rockchip_hdmi_cec_submit_work(int event, int delay, void *data)
{
	struct cec_delayed_work *work;

	CECDBG("%s event %04x delay %d\n", __func__, event, delay);

	work = kmalloc(sizeof(*work), GFP_ATOMIC);

	if (work) {
		INIT_DELAYED_WORK(&work->work, cecworkfunc);
		work->event = event;
		work->data = data;
		queue_delayed_work(cec_dev->workqueue,
				   &work->work,
				   msecs_to_jiffies(delay));
	} else {
		CECDBG(KERN_WARNING "CEC: Cannot allocate memory\n");
	}
}

void rockchip_hdmi_cec_set_pa(int devpa)
{
	if (cec_dev)
		cec_dev->address_phy = devpa;
	cecenumeration();
}

static int cec_input_device_init(void)
{
	int err, i;

	devinput = input_allocate_device();
	 if (!devinput)
		return -ENOMEM;
	devinput->name = "hdmi_cec_key";
	/*devinput->dev.parent = &client->dev;*/
	devinput->phys = "hdmi_cec_key/input0";
	devinput->id.bustype = BUS_HOST;
	devinput->id.vendor = 0x0001;
	devinput->id.product = 0x0001;
	devinput->id.version = 0x0100;
	err = input_register_device(devinput);
	if (err < 0) {
		input_free_device(devinput);
		CECDBG("%s input device error", __func__);
		return err;
	}
	for (i = 0; i < (sizeof(key_table)/sizeof(int)); i++)
		input_set_capability(devinput, EV_KEY, key_table[i]);
	return 0;
}

static void cecmenucontrol(int uitemp)
{
	switch (uitemp) {
	case S_CEC_MAKESURE:  /*make sure*/
		CECDBG("CEC UIcommand  makesure\n");
		input_event(devinput, EV_KEY, KEY_REPLY, 1);
		input_sync(devinput);
		input_event(devinput, EV_KEY, KEY_REPLY, 0);
		input_sync(devinput);
		break;
	case S_CEC_UP:  /*up*/
		CECDBG("CEC UIcommand  up\n");
		input_event(devinput, EV_KEY, KEY_UP, 1);
		input_sync(devinput);
		input_event(devinput, EV_KEY, KEY_UP, 0);
		input_sync(devinput);
		break;
	case S_CEC_DOWN:  /*down*/
		CECDBG("CEC UIcommand  down\n");
		input_event(devinput, EV_KEY, KEY_DOWN, 1);
		input_sync(devinput);
		input_event(devinput, EV_KEY, KEY_DOWN, 0);
		input_sync(devinput);
		break;
	case S_CEC_LEFT:  /*left*/
		CECDBG("CEC UIcommand  left\n");
		input_event(devinput, EV_KEY, KEY_LEFT , 1);
		input_sync(devinput);
		input_event(devinput, EV_KEY, KEY_LEFT , 0);
		input_sync(devinput);
		break;
	case S_CEC_RIGHT:  /*right*/
		CECDBG("CEC UIcommand  right\n");
		input_event(devinput, EV_KEY, KEY_RIGHT, 1);
		input_sync(devinput);
		input_event(devinput, EV_KEY, KEY_RIGHT, 0);
		input_sync(devinput);
		break;
	case S_CEC_BACK:  /*back*/
		CECDBG("CEC UIcommand  back\n");
		input_event(devinput, EV_KEY, KEY_BACK, 1);
		input_sync(devinput);
		input_event(devinput, EV_KEY, KEY_BACK, 0);
		input_sync(devinput);
		break;
	case S_CEC_VENDORBACK:
		CECDBG("CEC UIcommand  vendor back\n");
		input_event(devinput, EV_KEY, KEY_BACK, 1);
		input_sync(devinput);
		input_event(devinput, EV_KEY, KEY_BACK, 0);
		input_sync(devinput);
		break;
	}
}


static ssize_t  cec_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", cec_dev->cecval);
}

static ssize_t cec_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int ret;

	ret = sscanf(buf, "%s", cec_dev->cecval);
	return strnlen(buf, PAGE_SIZE);
}

static struct device_attribute cec_control_attr = {
	.attr = {.name = "cec", .mode = 0666},
	.show = cec_show,
	.store = cec_store,
};

int rockchip_hdmi_cec_init(struct hdmi *hdmi,
			   int (*sendframe)(struct hdmi *,
					    struct cec_framedata *),
			   int (*readframe)(struct hdmi *,
					    struct cec_framedata *),
			   void (*setceclogicaddr)(struct hdmi *, int))
{
	int ret;
	static int cecmicsdevflag = 1;

	mdev.minor = MISC_DYNAMIC_MINOR;
	mdev.name = "cec";
	mdev.mode = 0666;
	cec_dev = kmalloc(sizeof(*cec_dev), GFP_KERNEL);
	if (!cec_dev) {
		pr_err("HDMI CEC: kmalloc fail!");
		return -ENOMEM;
	}
	memset(cec_dev, 0, sizeof(struct cec_device));
	cec_dev->hdmi = hdmi;
	cec_dev->cecval[0] = '1';
	cec_dev->cecval[1] = '\0';
	cec_dev->sendframe = sendframe;
	cec_dev->readframe = readframe;
	cec_dev->setceclogicaddr = setceclogicaddr;
	cec_dev->workqueue = create_singlethread_workqueue("hdmi-cec");
	if (cec_dev->workqueue == NULL) {
		pr_err("HDMI CEC: create workqueue failed.\n");
		return -1;
	}
	if (cecmicsdevflag) {
		cec_input_device_init();
	if (misc_register(&mdev)) {
		pr_err("CEC: Could not add cec misc driver\n");
		goto error;
	}

	ret = device_create_file(mdev.this_device, &cec_control_attr);
	if (ret) {
		pr_err("CEC: Could not add sys file enable\n");
	goto error1;
	}
	cecmicsdevflag = 0;
	}
	return 0;

error1:
		misc_deregister(&mdev);
error:
		ret = -EINVAL;
	return ret;
}

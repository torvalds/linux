#include "rk3036_hdmi.h"
#include "rk3036_hdmi_hw.h"
#include "rk3036_hdmi_cec.h"

static cec_t cec;
static char la_player[3] = {CEC_LOGADDR_PLAYBACK1,
			    CEC_LOGADDR_PLAYBACK2,
			    CEC_LOGADDR_PLAYBACK3};

static int cec_read_frame(cec_framedata_t *frame)
{
	int i, length,val;
	char *data = (char *)frame;//modify by hjc
	if(frame == NULL)
		return -1;
	
	hdmi_readl(hdmi_dev, CEC_RX_LENGTH, &length);
	hdmi_writel(hdmi_dev, CEC_RX_OFFSET, 0);
	
	printk("CEC: %s length is %d\n", __FUNCTION__, length);
	for(i = 0; i < length; i++) {
		hdmi_readl(hdmi_dev, CEC_DATA, &val);
		data[i] = val;
		printk("%02x\n", data[i]);
	}
	return 0;
}

static int cec_send_frame(cec_framedata_t *frame)
{
	int i;
	
	CECDBG("CEC: TX srcdestaddr %02x opcode %02x ",
		 frame->srcdestaddr, frame->opcode);
	if(frame->argcount) {
		CECDBG("args:");
		for(i = 0; i < frame->argcount; i++) {
			CECDBG("%02x ", frame->args[i]);
		}
	}
	CECDBG("\n");

	hdmi_writel(hdmi_dev, CEC_TX_OFFSET, 0);
	hdmi_writel(hdmi_dev, CEC_DATA, frame->srcdestaddr);
	hdmi_writel(hdmi_dev, CEC_DATA, frame->opcode);

	for(i = 0; i < frame->argcount; i++)
		hdmi_writel(hdmi_dev, CEC_DATA, frame->args[i]);

	hdmi_writel(hdmi_dev, CEC_TX_LENGTH, frame->argcount + 2);
	
	/*Wait for bus free*/
	cec.busfree = 1;
	hdmi_writel(hdmi_dev, CEC_CTRL, m_BUSFREETIME_ENABLE);
	CECDBG("start wait bus free\n");
	if(wait_event_interruptible_timeout(cec.wait, cec.busfree == 0, msecs_to_jiffies(17))) {
		return -1;
	}
	CECDBG("end wait bus free,start tx,busfree=%d\n",cec.busfree);
	/*Start TX*/
	cec.tx_done = 0;
	hdmi_writel(hdmi_dev, CEC_CTRL, m_BUSFREETIME_ENABLE|m_START_TX);
	if(wait_event_interruptible_timeout(cec.wait, cec.tx_done != 0, msecs_to_jiffies(100)))
		hdmi_writel(hdmi_dev, CEC_CTRL, 0);
	CECDBG("end tx,tx_done=%d\n",cec.tx_done);
	if (cec.tx_done == 1) {
		cec.tx_done = 0;//hjc add ,need?
		return 0;
	} else
		return -1;
}

static int cec_send_message(char opcode, char dest)
{
	cec_framedata_t cecframe;
	CECDBG("CEC: cec_send_message\n");

	cecframe.opcode = opcode;
	cecframe.srcdestaddr = MAKE_SRCDEST(cec.address_logic, dest);
	cecframe.argcount = 0;
	return cec_send_frame(&cecframe);
}

static void cec_send_feature_abort ( cec_framedata_t *pcpi, char reason )
{
    cec_framedata_t cecFrame;
    CECDBG("CEC: cec_send_feature_abort\n");

    if (( pcpi->srcdestaddr & 0x0F) != CEC_LOGADDR_UNREGORBC )
    {
        cecFrame.opcode        = CECOP_FEATURE_ABORT;
        cecFrame.srcdestaddr   = MAKE_SRCDEST( cec.address_logic, (pcpi->srcdestaddr & 0xF0) >> 4 );
        cecFrame.args[0]       = pcpi->opcode;
        cecFrame.args[1]       = reason;
        cecFrame.argcount      = 2;
        cec_send_frame( &cecFrame );
    }
}

static void cec_send_active_source(void)
{
	cec_framedata_t    cecframe;
	
	CECDBG("CEC: start_active_source\n");
	cecframe.opcode        = CECOP_ACTIVE_SOURCE;
	cecframe.srcdestaddr   = MAKE_SRCDEST( cec.address_logic, CEC_LOGADDR_UNREGORBC);
	cecframe.args[0]       = (cec.address_phy & 0xFF00) >> 8;        /* [Physical Address]*/
	cecframe.args[1]       = (cec.address_phy & 0x00FF);             /* [Physical Address]*/
	cecframe.argcount      = 2;
	cec_send_frame( &cecframe );
}

static void start_active_source(void)
{
	int i;
	CECDBG("CEC: start_active_source\n");
	/* GPIO simulate CEC timing may be not correct, so we try more times.*/
	/*send image view on first*/
	for(i = 0; i < 1; i++) {
		if(cec_send_message(CECOP_IMAGE_VIEW_ON,CEC_LOGADDR_TV) == 0) {
			cec_send_active_source();
		}
	}
}

static void cec_handle_inactive_source ( cec_framedata_t *pcpi )
{
	
}

static void cec_handle_feature_abort( cec_framedata_t *pcpi )
{
   
}

static bool validate_cec_message(cec_framedata_t *pcpi)
{
	char parametercount = 0;
	bool countok = true;
	CECDBG("CEC: validate_cec_message,opcode=%d\n",pcpi->opcode);

	/* Determine required parameter count   */
	switch (pcpi->opcode)
	{
	case CECOP_IMAGE_VIEW_ON:
	case CECOP_TEXT_VIEW_ON:
	case CECOP_STANDBY:
	case CECOP_GIVE_PHYSICAL_ADDRESS:
	case CECOP_GIVE_DEVICE_POWER_STATUS:
	case CECOP_GET_MENU_LANGUAGE:
	case CECOP_GET_CEC_VERSION:
		parametercount = 0;
		break;
	case CECOP_REPORT_POWER_STATUS:         // power status*/
	case CECOP_CEC_VERSION:                 // cec version*/
		parametercount = 1;
		break;
	case CECOP_INACTIVE_SOURCE:             // physical address*/
	case CECOP_FEATURE_ABORT:               // feature opcode / abort reason*/
	case CECOP_ACTIVE_SOURCE:               // physical address*/
		parametercount = 2;
		break;
	case CECOP_REPORT_PHYSICAL_ADDRESS:     // physical address / device type*/
	case CECOP_DEVICE_VENDOR_ID:            // vendor id*/
		parametercount = 3;
		break;
	case CECOP_SET_OSD_NAME:                // osd name (1-14 bytes)*/
	case CECOP_SET_OSD_STRING:              // 1 + x   display control / osd string (1-13 bytes)*/
		parametercount = 1;                 // must have a minimum of 1 operands*/
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

    	if (pcpi->argcount < parametercount) {
		CECDBG("CEC: pcpi->argcount[%d] < parametercount[%d]\n",
			pcpi->argcount,parametercount);
        	countok = false;
    	}
    	return(countok );
}

static bool cec_rx_msg_handler_last(cec_framedata_t *pcpi)
{
	bool isdirectaddressed;
	cec_framedata_t cecFrame;
	
	CECDBG("CEC: cec_rx_msg_handler_last,opcode=%d\n",pcpi->opcode);
	isdirectaddressed = !((pcpi->srcdestaddr & 0x0F) == CEC_LOGADDR_UNREGORBC);

	if (validate_cec_message(pcpi)) {/* If invalid message, ignore it, but treat it as handled*/
	        if (isdirectaddressed) {
			switch (pcpi->opcode) {
			case CECOP_FEATURE_ABORT:
				cec_handle_feature_abort(pcpi);
				break;
			case CECOP_IMAGE_VIEW_ON:       /*In our case, respond the same to both these messages*/
			case CECOP_TEXT_VIEW_ON:
				break;
			case CECOP_STANDBY:             /* Direct and Broadcast*/
	                        /* Setting this here will let the main task know    */
	                        /* (via SI_CecGetPowerState) and at the same time   */
	                        /* prevent us from broadcasting a STANDBY message   */
	                        /* of our own when the main task responds by        */
	                        /* calling SI_CecSetPowerState( STANDBY );          */
				cec.powerstatus = CEC_POWERSTATUS_STANDBY;
				break;
			case CECOP_INACTIVE_SOURCE:
				cec_handle_inactive_source(pcpi);
				break;
			case CECOP_GIVE_PHYSICAL_ADDRESS:
				/* TV responds by broadcasting its Physical Address: 0.0.0.0   */
				cecFrame.opcode        = CECOP_REPORT_PHYSICAL_ADDRESS;
				cecFrame.srcdestaddr   = MAKE_SRCDEST(cec.address_logic,
								      CEC_LOGADDR_UNREGORBC);
				cecFrame.args[0]       = (cec.address_phy&0xFF00)>>8;             /* [Physical Address]*/
				cecFrame.args[1]       = (cec.address_phy&0x00FF);             /*[Physical Address]*/
				cecFrame.args[2]       = cec.address_logic;/*CEC_LOGADDR_PLAYBACK1;//2011.08.03 CEC_LOGADDR_TV;   // [Device Type] = 0 = TV*/
				cecFrame.argcount      = 3;
				cec_send_frame(&cecFrame);
				break;
			case CECOP_GIVE_DEVICE_POWER_STATUS:
				/* TV responds with power status.   */
				cecFrame.opcode        = CECOP_REPORT_POWER_STATUS;
				cecFrame.srcdestaddr   = MAKE_SRCDEST(cec.address_logic, (pcpi->srcdestaddr & 0xF0) >> 4);
				cecFrame.args[0]       = cec.powerstatus;
				cecFrame.argcount      = 1;
				cec_send_frame(&cecFrame);
				break;
			case CECOP_GET_MENU_LANGUAGE:
				/* TV Responds with a Set Menu language command.    */
				cecFrame.opcode         = CECOP_SET_MENU_LANGUAGE;
				cecFrame.srcdestaddr    = CEC_LOGADDR_UNREGORBC;
				cecFrame.args[0]        = 'e';     /* [language code see iso/fdis 639-2]*/
				cecFrame.args[1]        = 'n';     /* [language code see iso/fdis 639-2]*/
				cecFrame.args[2]        = 'g';     /* [language code see iso/fdis 639-2]*/
				cecFrame.argcount       = 3;
				cec_send_frame(&cecFrame);
				break;
			case CECOP_GET_CEC_VERSION:
	                    /* TV responds to this request with it's CEC version support.   */
				cecFrame.srcdestaddr   = MAKE_SRCDEST(cec.address_logic, (pcpi->srcdestaddr & 0xF0) >> 4);
				cecFrame.opcode        = CECOP_CEC_VERSION;
				cecFrame.args[0]       = 0x04;       /* Report CEC1.3a*/
				cecFrame.argcount      = 1;
				cec_send_frame(&cecFrame);
				break;
			case CECOP_REPORT_POWER_STATUS:         /* Someone sent us their power state.*/
				/*l_sourcePowerStatus = pcpi->args[0];
				Let NEW SOURCE task know about it.   
				if  (l_cecTaskState.task == SI_CECTASK_NEWSOURCE)  {
				l_cecTaskState.cpiState = CPI_RESPONSE;
				}*/
				break;
	                /* Do not reply to directly addressed 'Broadcast' msgs.  */
			case CECOP_ACTIVE_SOURCE:
			case CECOP_REPORT_PHYSICAL_ADDRESS:     /*A physical address was broadcast -- ignore it.*/
			case CECOP_REQUEST_ACTIVE_SOURCE:       /*We are not a source, so ignore this one.*/
			case CECOP_ROUTING_CHANGE:              /*We are not a downstream switch, so ignore this one.*/
			case CECOP_ROUTING_INFORMATION:         /*We are not a downstream switch, so ignore this one.*/
			case CECOP_SET_STREAM_PATH:             /*We are not a source, so ignore this one.*/
			case CECOP_SET_MENU_LANGUAGE:           /*As a TV, we can ignore this message*/
			case CECOP_DEVICE_VENDOR_ID:
				break;
			case CECOP_ABORT:       /*Send Feature Abort for all unsupported features.*/
			default:
				cec_send_feature_abort(pcpi, CECAR_UNRECOG_OPCODE);
				break;
			}
		} else {
	        /* Respond to broadcast messages. */
			switch (pcpi->opcode) {
			case CECOP_STANDBY:

	                        /* Setting this here will let the main task know    */
	                        /* (via SI_CecGetPowerState) and at the same time   */
	                        /* prevent us from broadcasting a STANDBY message   */
	                        /* of our own when the main task responds by        */
	                        /* calling SI_CecSetPowerState( STANDBY );          */

	                    cec.powerstatus = CEC_POWERSTATUS_STANDBY;
	                    break;

			case CECOP_ACTIVE_SOURCE:
			/*CecHandleActiveSource( pcpi );*/
				break;
			case CECOP_REPORT_PHYSICAL_ADDRESS:
			/*CecHandleReportPhysicalAddress( pcpi );*/
				break;
			/* Do not reply to 'Broadcast' msgs that we don't need.  */
			case CECOP_REQUEST_ACTIVE_SOURCE:       /*We are not a source, so ignore this one.*/
			/*SI_StartActiveSource(0,0);//2011.08.03*/
				break;
			case CECOP_ROUTING_CHANGE:              /*We are not a downstream switch, so ignore this one.*/
			case CECOP_ROUTING_INFORMATION:         /*We are not a downstream switch, so ignore this one.*/
			case CECOP_SET_STREAM_PATH:             /*We are not a source, so ignore this one.*/
			case CECOP_SET_MENU_LANGUAGE:           /*As a TV, we can ignore this message*/
				break;
			}
		}
	}
	return 0;
}

static void cec_work_func(struct work_struct *work)
{
	struct cec_delayed_work *cec_w =
		container_of(work, struct cec_delayed_work, work.work);
	cec_framedata_t cecFrame;
	CECDBG(KERN_WARNING "CEC: cec_work_func,event=%d\n",cec_w->event);
	switch(cec_w->event)
	{
		case EVENT_ENUMERATE:
			break;
		case EVENT_RX_FRAME:
			memset(&cecFrame, 0, sizeof(cec_framedata_t));
			cec_read_frame(&cecFrame);
			cec_rx_msg_handler_last(&cecFrame);
			break;
		default:
			break;
	}

	if(cec_w->data)
		kfree(cec_w->data);
	kfree(cec_w);
}

static void cec_submit_work(int event, int delay, void *data)
{
	struct cec_delayed_work *work;

	CECDBG("%s event %04x delay %d", __func__, event, delay);
	work = kmalloc(sizeof(struct cec_delayed_work), GFP_ATOMIC);

	if (work) {
		INIT_DELAYED_WORK(&work->work, cec_work_func);
		work->event = event;
		work->data = data;
		queue_delayed_work(cec.workqueue,
				   &work->work,
				   msecs_to_jiffies(delay));
	} else {
		CECDBG(KERN_WARNING "CEC: GPIO CEC: Cannot allocate memory to "
				    "create work\n");;
	}
}

int cec_enumerate(void)
{
	/*for(i = 0; i < 3; i++) {
		if(Cec_Ping(la_player[i]) == 1) {
			cec.address_logic = la_player[i];
			break;
		}
	}
	if(i == 3)
		return -1;
	//Broadcast our physical address.
	GPIO_CecSendMessage(CECOP_GET_MENU_LANGUAGE,CEC_LOGADDR_TV);
	msleep(100);*/
	CECDBG("CEC: %s\n", __func__);	
	cec.address_logic = la_player[0];
	hdmi_writel(hdmi_dev, CEC_LOGICADDR, cec.address_logic);
	start_active_source();
	return 0;
}

void cec_set_device_pa(int addr)
{
	CECDBG("CEC: Physical Address is %02x", addr);
	cec.address_phy = addr;
}

void rk3036_cec_isr(void)
{
	int tx_isr = 0, rx_isr = 0;
	hdmi_readl(hdmi_dev, CEC_TX_INT, &tx_isr);
	hdmi_readl(hdmi_dev, CEC_RX_INT, &rx_isr);

	CECDBG("CEC: rk3036_cec_isr:tx_isr %02x  rx_isr %02x\n\n", tx_isr, rx_isr);
	
	hdmi_writel(hdmi_dev, CEC_TX_INT, tx_isr);
	hdmi_writel(hdmi_dev, CEC_RX_INT, rx_isr);
	
	if (tx_isr & m_TX_BUSNOTFREE) {
		cec.busfree = 0;
		CECDBG("CEC: m_TX_BUSNOTFREE,busfree=%d\n",cec.busfree);		
	} else if (tx_isr & m_TX_DONE) {
		cec.tx_done = 1;
		CECDBG("CEC: m_TX_DONE,busfree=%d\n",cec.tx_done);
	} else {
		cec.tx_done = -1;
		CECDBG("CEC: else:busfree=%d\n",cec.tx_done);
	}	
	
	wake_up_interruptible_all(&cec.wait);
	if(rx_isr & m_RX_DONE)
		cec_submit_work(EVENT_RX_FRAME, 0, NULL);
}


static int __init rk3036_cec_init(void)
{
	CECDBG(KERN_ERR "CEC: rk3036_cec_init start.\n");
	memset(&cec, 0, sizeof(cec_t));
	cec.workqueue = create_singlethread_workqueue("cec");
	if (cec.workqueue == NULL) {
		CECDBG(KERN_ERR "CEC: GPIO CEC: create workqueue failed.\n");
		return -1;
	}
	init_waitqueue_head(&cec.wait);
	
	/*Fref = Fsys / ((register 0xd4 + 1)*(register 0xd5 + 1))*/
	/*Fref = 0.5M, Fsys = 74.25M*/
	hdmi_writel(hdmi_dev, CEC_CLK_H, 11);
	hdmi_writel(hdmi_dev, CEC_CLK_L, 11);

	/*Set bus free time to 16.8ms*/
	hdmi_writel(hdmi_dev, CEC_BUSFREETIME_L, 0xd0);
	hdmi_writel(hdmi_dev, CEC_BUSFREETIME_H, 0x20);	
	
	/*Enable TX/RX INT*/
	hdmi_writel(hdmi_dev, CEC_TX_INT, 0xFF);
	hdmi_writel(hdmi_dev, CEC_RX_INT, 0xFF);

	rk3036_hdmi_register_cec_callbacks(rk3036_cec_isr,
					   cec_set_device_pa,
					   cec_enumerate);
	
	CECDBG(KERN_ERR "CEC: rk3036_cec_init sucess\n");

	return 0;
}

static void __exit rk3036_cec_exit(void)
{
	kfree(&cec);
}

late_initcall_sync(rk3036_cec_init);
module_exit(rk3036_cec_exit);

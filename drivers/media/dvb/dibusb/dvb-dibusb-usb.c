/*
 * dvb-dibusb-usb.c is part of the driver for mobile USB Budget DVB-T devices
 * based on reference design made by DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * see dvb-dibusb-core.c for more copyright details.
 *
 * This file contains functions for initializing and handling the
 * usb specific stuff.
 */
#include "dvb-dibusb.h"

#include <linux/version.h>
#include <linux/pci.h>

int dibusb_readwrite_usb(struct usb_dibusb *dib, u8 *wbuf, u16 wlen, u8 *rbuf,
		u16 rlen)
{
	int actlen,ret = -ENOMEM;

	if (wbuf == NULL || wlen == 0)
		return -EINVAL;

	if ((ret = down_interruptible(&dib->usb_sem)))
		return ret;

	debug_dump(wbuf,wlen);

	ret = usb_bulk_msg(dib->udev,usb_sndbulkpipe(dib->udev,
			dib->dibdev->dev_cl->pipe_cmd), wbuf,wlen,&actlen,
			DIBUSB_I2C_TIMEOUT);

	if (ret)
		err("bulk message failed: %d (%d/%d)",ret,wlen,actlen);
	else
		ret = actlen != wlen ? -1 : 0;

	/* an answer is expected, and no error before */
	if (!ret && rbuf && rlen) {
		ret = usb_bulk_msg(dib->udev,usb_rcvbulkpipe(dib->udev,
				dib->dibdev->dev_cl->pipe_cmd),rbuf,rlen,&actlen,
				DIBUSB_I2C_TIMEOUT);

		if (ret)
			err("recv bulk message failed: %d",ret);
		else {
			deb_alot("rlen: %d\n",rlen);
			debug_dump(rbuf,actlen);
		}
	}

	up(&dib->usb_sem);
	return ret;
}

/*
 * Cypress controls
 */
int dibusb_write_usb(struct usb_dibusb *dib, u8 *buf, u16 len)
{
	return dibusb_readwrite_usb(dib,buf,len,NULL,0);
}

#if 0
/*
 * #if 0'ing the following functions as they are not in use _now_,
 * but probably will be sometime.
 */
/*
 * do not use this, just a workaround for a bug,
 * which will hopefully never occur :).
 */
int dibusb_interrupt_read_loop(struct usb_dibusb *dib)
{
	u8 b[1] = { DIBUSB_REQ_INTR_READ };
	return dibusb_write_usb(dib,b,1);
}
#endif

/*
 * ioctl for the firmware
 */
static int dibusb_ioctl_cmd(struct usb_dibusb *dib, u8 cmd, u8 *param, int plen)
{
	u8 b[34];
	int size = plen > 32 ? 32 : plen;
	memset(b,0,34);
	b[0] = DIBUSB_REQ_SET_IOCTL;
	b[1] = cmd;

	if (size > 0)
		memcpy(&b[2],param,size);

	return dibusb_write_usb(dib,b,34); //2+size);
}

/*
 * ioctl for power control
 */
int dibusb_hw_wakeup(struct dvb_frontend *fe)
{
	struct usb_dibusb *dib = (struct usb_dibusb *) fe->dvb->priv;
	u8 b[1] = { DIBUSB_IOCTL_POWER_WAKEUP };
	deb_info("dibusb-device is getting up.\n");

	switch (dib->dibdev->dev_cl->id) {
		case DTT200U:
			break;
		default:
			dibusb_ioctl_cmd(dib,DIBUSB_IOCTL_CMD_POWER_MODE, b,1);
			break;
	}

	if (dib->fe_init)
		return dib->fe_init(fe);

	return 0;
}

int dibusb_hw_sleep(struct dvb_frontend *fe)
{
	struct usb_dibusb *dib = (struct usb_dibusb *) fe->dvb->priv;
	u8 b[1] = { DIBUSB_IOCTL_POWER_SLEEP };
	deb_info("dibusb-device is going to bed.\n");
	/* workaround, something is wrong, when dibusb 1.1 device are going to bed too late */
	switch (dib->dibdev->dev_cl->id) {
		case DIBUSB1_1:
		case NOVAT_USB2:
		case DTT200U:
			break;
		default:
			dibusb_ioctl_cmd(dib,DIBUSB_IOCTL_CMD_POWER_MODE, b,1);
			break;
	}
	if (dib->fe_sleep)
		return dib->fe_sleep(fe);

	return 0;
}

int dibusb_set_streaming_mode(struct usb_dibusb *dib,u8 mode)
{
	u8 b[2] = { DIBUSB_REQ_SET_STREAMING_MODE, mode };
	return dibusb_readwrite_usb(dib,b,2,NULL,0);
}

static int dibusb_urb_kill(struct usb_dibusb *dib)
{
	int i;
deb_info("trying to kill urbs\n");
	if (dib->init_state & DIBUSB_STATE_URB_SUBMIT) {
		for (i = 0; i < dib->dibdev->dev_cl->urb_count; i++) {
			deb_info("killing URB no. %d.\n",i);

			/* stop the URB */
			usb_kill_urb(dib->urb_list[i]);
		}
	} else
	deb_info(" URBs not killed.\n");
	dib->init_state &= ~DIBUSB_STATE_URB_SUBMIT;
	return 0;
}

static int dibusb_urb_submit(struct usb_dibusb *dib)
{
	int i,ret;
	if (dib->init_state & DIBUSB_STATE_URB_INIT) {
		for (i = 0; i < dib->dibdev->dev_cl->urb_count; i++) {
			deb_info("submitting URB no. %d\n",i);
			if ((ret = usb_submit_urb(dib->urb_list[i],GFP_ATOMIC))) {
				err("could not submit buffer urb no. %d - get them all back\n",i);
				dibusb_urb_kill(dib);
				return ret;
			}
			dib->init_state |= DIBUSB_STATE_URB_SUBMIT;
		}
	}
	return 0;
}

int dibusb_streaming(struct usb_dibusb *dib,int onoff)
{
	if (onoff)
		dibusb_urb_submit(dib);
	else
		dibusb_urb_kill(dib);

	switch (dib->dibdev->dev_cl->id) {
		case DIBUSB2_0:
		case DIBUSB2_0B:
		case NOVAT_USB2:
		case UMT2_0:
			if (onoff)
				return dibusb_ioctl_cmd(dib,DIBUSB_IOCTL_CMD_ENABLE_STREAM,NULL,0);
			else
				return dibusb_ioctl_cmd(dib,DIBUSB_IOCTL_CMD_DISABLE_STREAM,NULL,0);
			break;
		default:
			break;
	}
	return 0;
}

int dibusb_urb_init(struct usb_dibusb *dib)
{
	int i,bufsize,def_pid_parse = 1;

	/*
	 * when reloading the driver w/o replugging the device
	 * a timeout occures, this helps
	 */
	usb_clear_halt(dib->udev,usb_sndbulkpipe(dib->udev,dib->dibdev->dev_cl->pipe_cmd));
	usb_clear_halt(dib->udev,usb_rcvbulkpipe(dib->udev,dib->dibdev->dev_cl->pipe_cmd));
	usb_clear_halt(dib->udev,usb_rcvbulkpipe(dib->udev,dib->dibdev->dev_cl->pipe_data));

	/* allocate the array for the data transfer URBs */
	dib->urb_list = kmalloc(dib->dibdev->dev_cl->urb_count*sizeof(struct urb *),GFP_KERNEL);
	if (dib->urb_list == NULL)
		return -ENOMEM;
	memset(dib->urb_list,0,dib->dibdev->dev_cl->urb_count*sizeof(struct urb *));

	dib->init_state |= DIBUSB_STATE_URB_LIST;

	bufsize = dib->dibdev->dev_cl->urb_count*dib->dibdev->dev_cl->urb_buffer_size;
	deb_info("allocate %d bytes as buffersize for all URBs\n",bufsize);
	/* allocate the actual buffer for the URBs */
	if ((dib->buffer = pci_alloc_consistent(NULL,bufsize,&dib->dma_handle)) == NULL) {
		deb_info("not enough memory.\n");
		return -ENOMEM;
	}
	deb_info("allocation complete\n");
	memset(dib->buffer,0,bufsize);

	dib->init_state |= DIBUSB_STATE_URB_BUF;

	/* allocate and submit the URBs */
	for (i = 0; i < dib->dibdev->dev_cl->urb_count; i++) {
		if (!(dib->urb_list[i] = usb_alloc_urb(0,GFP_ATOMIC))) {
			return -ENOMEM;
		}

		usb_fill_bulk_urb( dib->urb_list[i], dib->udev,
				usb_rcvbulkpipe(dib->udev,dib->dibdev->dev_cl->pipe_data),
				&dib->buffer[i*dib->dibdev->dev_cl->urb_buffer_size],
				dib->dibdev->dev_cl->urb_buffer_size,
				dibusb_urb_complete, dib);

		dib->urb_list[i]->transfer_flags = 0;

		dib->init_state |= DIBUSB_STATE_URB_INIT;
	}

	/* dib->pid_parse here contains the value of the module parameter */
	/* decide if pid parsing can be deactivated:
	 * is possible (by device type) and wanted (by user)
	 */
	switch (dib->dibdev->dev_cl->id) {
		case DIBUSB2_0:
		case DIBUSB2_0B:
			if (dib->udev->speed == USB_SPEED_HIGH && !dib->pid_parse) {
				def_pid_parse = 0;
				info("running at HIGH speed, will deliver the complete TS.");
			} else
				info("will use pid_parsing.");
			break;
		default:
			break;
	}
	/* from here on it contains the device and user decision */
	dib->pid_parse = def_pid_parse;

	return 0;
}

int dibusb_urb_exit(struct usb_dibusb *dib)
{
	int i;

	dibusb_urb_kill(dib);

	if (dib->init_state & DIBUSB_STATE_URB_LIST) {
		for (i = 0; i < dib->dibdev->dev_cl->urb_count; i++) {
			if (dib->urb_list[i] != NULL) {
				deb_info("freeing URB no. %d.\n",i);
				/* free the URBs */
				usb_free_urb(dib->urb_list[i]);
			}
		}
		/* free the urb array */
		kfree(dib->urb_list);
		dib->init_state &= ~DIBUSB_STATE_URB_LIST;
	}

	if (dib->init_state & DIBUSB_STATE_URB_BUF)
		pci_free_consistent(NULL,
			dib->dibdev->dev_cl->urb_buffer_size*dib->dibdev->dev_cl->urb_count,
			dib->buffer,dib->dma_handle);

	dib->init_state &= ~DIBUSB_STATE_URB_BUF;
	dib->init_state &= ~DIBUSB_STATE_URB_INIT;
	return 0;
}

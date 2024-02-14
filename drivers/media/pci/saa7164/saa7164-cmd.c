// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
 */

#include <linux/wait.h>

#include "saa7164.h"

static int saa7164_cmd_alloc_seqno(struct saa7164_dev *dev)
{
	int i, ret = -1;

	mutex_lock(&dev->lock);
	for (i = 0; i < SAA_CMD_MAX_MSG_UNITS; i++) {
		if (dev->cmds[i].inuse == 0) {
			dev->cmds[i].inuse = 1;
			dev->cmds[i].signalled = 0;
			dev->cmds[i].timeout = 0;
			ret = dev->cmds[i].seqno;
			break;
		}
	}
	mutex_unlock(&dev->lock);

	return ret;
}

static void saa7164_cmd_free_seqno(struct saa7164_dev *dev, u8 seqno)
{
	mutex_lock(&dev->lock);
	if ((dev->cmds[seqno].inuse == 1) &&
		(dev->cmds[seqno].seqno == seqno)) {
		dev->cmds[seqno].inuse = 0;
		dev->cmds[seqno].signalled = 0;
		dev->cmds[seqno].timeout = 0;
	}
	mutex_unlock(&dev->lock);
}

static void saa7164_cmd_timeout_seqno(struct saa7164_dev *dev, u8 seqno)
{
	mutex_lock(&dev->lock);
	if ((dev->cmds[seqno].inuse == 1) &&
		(dev->cmds[seqno].seqno == seqno)) {
		dev->cmds[seqno].timeout = 1;
	}
	mutex_unlock(&dev->lock);
}

static u32 saa7164_cmd_timeout_get(struct saa7164_dev *dev, u8 seqno)
{
	int ret = 0;

	mutex_lock(&dev->lock);
	if ((dev->cmds[seqno].inuse == 1) &&
		(dev->cmds[seqno].seqno == seqno)) {
		ret = dev->cmds[seqno].timeout;
	}
	mutex_unlock(&dev->lock);

	return ret;
}

/* Commands to the f/w get marshelled to/from this code then onto the PCI
 * -bus/c running buffer. */
int saa7164_irq_dequeue(struct saa7164_dev *dev)
{
	int ret = SAA_OK, i = 0;
	u32 timeout;
	wait_queue_head_t *q = NULL;
	u8 tmp[512];
	dprintk(DBGLVL_CMD, "%s()\n", __func__);

	/* While any outstand message on the bus exists... */
	do {

		/* Peek the msg bus */
		struct tmComResInfo tRsp = { 0, 0, 0, 0, 0, 0 };
		ret = saa7164_bus_get(dev, &tRsp, NULL, 1);
		if (ret != SAA_OK)
			break;

		q = &dev->cmds[tRsp.seqno].wait;
		timeout = saa7164_cmd_timeout_get(dev, tRsp.seqno);
		dprintk(DBGLVL_CMD, "%s() timeout = %d\n", __func__, timeout);
		if (!timeout) {
			dprintk(DBGLVL_CMD,
				"%s() signalled seqno(%d) (for dequeue)\n",
				__func__, tRsp.seqno);
			dev->cmds[tRsp.seqno].signalled = 1;
			wake_up(q);
		} else {
			printk(KERN_ERR
				"%s() found timed out command on the bus\n",
					__func__);

			/* Clean the bus */
			ret = saa7164_bus_get(dev, &tRsp, &tmp, 0);
			printk(KERN_ERR "%s() ret = %x\n", __func__, ret);
			if (ret == SAA_ERR_EMPTY)
				/* Someone else already fetched the response */
				return SAA_OK;

			if (ret != SAA_OK)
				return ret;
		}

		/* It's unlikely to have more than 4 or 5 pending messages,
		 * ensure we exit at some point regardless.
		 */
	} while (i++ < 32);

	return ret;
}

/* Commands to the f/w get marshelled to/from this code then onto the PCI
 * -bus/c running buffer. */
static int saa7164_cmd_dequeue(struct saa7164_dev *dev)
{
	int ret;
	u32 timeout;
	wait_queue_head_t *q = NULL;
	u8 tmp[512];
	dprintk(DBGLVL_CMD, "%s()\n", __func__);

	while (true) {

		struct tmComResInfo tRsp = { 0, 0, 0, 0, 0, 0 };
		ret = saa7164_bus_get(dev, &tRsp, NULL, 1);
		if (ret == SAA_ERR_EMPTY)
			return SAA_OK;

		if (ret != SAA_OK)
			return ret;

		q = &dev->cmds[tRsp.seqno].wait;
		timeout = saa7164_cmd_timeout_get(dev, tRsp.seqno);
		dprintk(DBGLVL_CMD, "%s() timeout = %d\n", __func__, timeout);
		if (timeout) {
			printk(KERN_ERR "found timed out command on the bus\n");

			/* Clean the bus */
			ret = saa7164_bus_get(dev, &tRsp, &tmp, 0);
			printk(KERN_ERR "ret = %x\n", ret);
			if (ret == SAA_ERR_EMPTY)
				/* Someone else already fetched the response */
				return SAA_OK;

			if (ret != SAA_OK)
				return ret;

			if (tRsp.flags & PVC_CMDFLAG_CONTINUE)
				printk(KERN_ERR "split response\n");
			else
				saa7164_cmd_free_seqno(dev, tRsp.seqno);

			printk(KERN_ERR " timeout continue\n");
			continue;
		}

		dprintk(DBGLVL_CMD, "%s() signalled seqno(%d) (for dequeue)\n",
			__func__, tRsp.seqno);
		dev->cmds[tRsp.seqno].signalled = 1;
		wake_up(q);
		return SAA_OK;
	}
}

static int saa7164_cmd_set(struct saa7164_dev *dev, struct tmComResInfo *msg,
			   void *buf)
{
	struct tmComResBusInfo *bus = &dev->bus;
	u8 cmd_sent;
	u16 size, idx;
	u32 cmds;
	void *tmp;
	int ret = -1;

	if (!msg) {
		printk(KERN_ERR "%s() !msg\n", __func__);
		return SAA_ERR_BAD_PARAMETER;
	}

	mutex_lock(&dev->cmds[msg->id].lock);

	size = msg->size;
	cmds = size / bus->m_wMaxReqSize;
	if (size % bus->m_wMaxReqSize == 0)
		cmds -= 1;

	cmd_sent = 0;

	/* Split the request into smaller chunks */
	for (idx = 0; idx < cmds; idx++) {

		msg->flags |= SAA_CMDFLAG_CONTINUE;
		msg->size = bus->m_wMaxReqSize;
		tmp = buf + idx * bus->m_wMaxReqSize;

		ret = saa7164_bus_set(dev, msg, tmp);
		if (ret != SAA_OK) {
			printk(KERN_ERR "%s() set failed %d\n", __func__, ret);

			if (cmd_sent) {
				ret = SAA_ERR_BUSY;
				goto out;
			}
			ret = SAA_ERR_OVERFLOW;
			goto out;
		}
		cmd_sent = 1;
	}

	/* If not the last command... */
	if (idx != 0)
		msg->flags &= ~SAA_CMDFLAG_CONTINUE;

	msg->size = size - idx * bus->m_wMaxReqSize;

	ret = saa7164_bus_set(dev, msg, buf + idx * bus->m_wMaxReqSize);
	if (ret != SAA_OK) {
		printk(KERN_ERR "%s() set last failed %d\n", __func__, ret);

		if (cmd_sent) {
			ret = SAA_ERR_BUSY;
			goto out;
		}
		ret = SAA_ERR_OVERFLOW;
		goto out;
	}
	ret = SAA_OK;

out:
	mutex_unlock(&dev->cmds[msg->id].lock);
	return ret;
}

/* Wait for a signal event, without holding a mutex. Either return TIMEOUT if
 * the event never occurred, or SAA_OK if it was signaled during the wait.
 */
static int saa7164_cmd_wait(struct saa7164_dev *dev, u8 seqno)
{
	wait_queue_head_t *q = NULL;
	int ret = SAA_BUS_TIMEOUT;
	unsigned long stamp;
	int r;

	if (saa_debug >= 4)
		saa7164_bus_dump(dev);

	dprintk(DBGLVL_CMD, "%s(seqno=%d)\n", __func__, seqno);

	mutex_lock(&dev->lock);
	if ((dev->cmds[seqno].inuse == 1) &&
		(dev->cmds[seqno].seqno == seqno)) {
		q = &dev->cmds[seqno].wait;
	}
	mutex_unlock(&dev->lock);

	if (q) {
		/* If we haven't been signalled we need to wait */
		if (dev->cmds[seqno].signalled == 0) {
			stamp = jiffies;
			dprintk(DBGLVL_CMD,
				"%s(seqno=%d) Waiting (signalled=%d)\n",
				__func__, seqno, dev->cmds[seqno].signalled);

			/* Wait for signalled to be flagged or timeout */
			/* In a highly stressed system this can easily extend
			 * into multiple seconds before the deferred worker
			 * is scheduled, and we're woken up via signal.
			 * We typically are signalled in < 50ms but it can
			 * take MUCH longer.
			 */
			wait_event_timeout(*q, dev->cmds[seqno].signalled,
				(HZ * waitsecs));
			r = time_before(jiffies, stamp + (HZ * waitsecs));
			if (r)
				ret = SAA_OK;
			else
				saa7164_cmd_timeout_seqno(dev, seqno);

			dprintk(DBGLVL_CMD, "%s(seqno=%d) Waiting res = %d (signalled=%d)\n",
				__func__, seqno, r,
				dev->cmds[seqno].signalled);
		} else
			ret = SAA_OK;
	} else
		printk(KERN_ERR "%s(seqno=%d) seqno is invalid\n",
			__func__, seqno);

	return ret;
}

void saa7164_cmd_signal(struct saa7164_dev *dev, u8 seqno)
{
	int i;
	dprintk(DBGLVL_CMD, "%s()\n", __func__);

	mutex_lock(&dev->lock);
	for (i = 0; i < SAA_CMD_MAX_MSG_UNITS; i++) {
		if (dev->cmds[i].inuse == 1) {
			dprintk(DBGLVL_CMD,
				"seqno %d inuse, sig = %d, t/out = %d\n",
				dev->cmds[i].seqno,
				dev->cmds[i].signalled,
				dev->cmds[i].timeout);
		}
	}

	for (i = 0; i < SAA_CMD_MAX_MSG_UNITS; i++) {
		if ((dev->cmds[i].inuse == 1) && ((i == 0) ||
			(dev->cmds[i].signalled) || (dev->cmds[i].timeout))) {
			dprintk(DBGLVL_CMD, "%s(seqno=%d) calling wake_up\n",
				__func__, i);
			dev->cmds[i].signalled = 1;
			wake_up(&dev->cmds[i].wait);
		}
	}
	mutex_unlock(&dev->lock);
}

int saa7164_cmd_send(struct saa7164_dev *dev, u8 id, enum tmComResCmd command,
	u16 controlselector, u16 size, void *buf)
{
	struct tmComResInfo command_t, *pcommand_t;
	struct tmComResInfo response_t, *presponse_t;
	u8 errdata[256];
	u16 resp_dsize;
	u16 data_recd;
	u32 loop;
	int ret;
	int safety = 0;

	dprintk(DBGLVL_CMD, "%s(unitid = %s (%d) , command = 0x%x, sel = 0x%x)\n",
		__func__, saa7164_unitid_name(dev, id), id,
		command, controlselector);

	if ((size == 0) || (buf == NULL)) {
		printk(KERN_ERR "%s() Invalid param\n", __func__);
		return SAA_ERR_BAD_PARAMETER;
	}

	/* Prepare some basic command/response structures */
	memset(&command_t, 0, sizeof(command_t));
	memset(&response_t, 0, sizeof(response_t));
	pcommand_t = &command_t;
	presponse_t = &response_t;
	command_t.id = id;
	command_t.command = command;
	command_t.controlselector = controlselector;
	command_t.size = size;

	/* Allocate a unique sequence number */
	ret = saa7164_cmd_alloc_seqno(dev);
	if (ret < 0) {
		printk(KERN_ERR "%s() No free sequences\n", __func__);
		ret = SAA_ERR_NO_RESOURCES;
		goto out;
	}

	command_t.seqno = (u8)ret;

	/* Send Command */
	resp_dsize = size;
	pcommand_t->size = size;

	dprintk(DBGLVL_CMD, "%s() pcommand_t.seqno = %d\n",
		__func__, pcommand_t->seqno);

	dprintk(DBGLVL_CMD, "%s() pcommand_t.size = %d\n",
		__func__, pcommand_t->size);

	ret = saa7164_cmd_set(dev, pcommand_t, buf);
	if (ret != SAA_OK) {
		printk(KERN_ERR "%s() set command failed %d\n", __func__, ret);

		if (ret != SAA_ERR_BUSY)
			saa7164_cmd_free_seqno(dev, pcommand_t->seqno);
		else
			/* Flag a timeout, because at least one
			 * command was sent */
			saa7164_cmd_timeout_seqno(dev, pcommand_t->seqno);

		goto out;
	}

	/* With split responses we have to collect the msgs piece by piece */
	data_recd = 0;
	loop = 1;
	while (loop) {
		dprintk(DBGLVL_CMD, "%s() loop\n", __func__);

		ret = saa7164_cmd_wait(dev, pcommand_t->seqno);
		dprintk(DBGLVL_CMD, "%s() loop ret = %d\n", __func__, ret);

		/* if power is down and this is not a power command ... */

		if (ret == SAA_BUS_TIMEOUT) {
			printk(KERN_ERR "Event timed out\n");
			saa7164_cmd_timeout_seqno(dev, pcommand_t->seqno);
			return ret;
		}

		if (ret != SAA_OK) {
			printk(KERN_ERR "spurious error\n");
			return ret;
		}

		/* Peek response */
		ret = saa7164_bus_get(dev, presponse_t, NULL, 1);
		if (ret == SAA_ERR_EMPTY) {
			dprintk(4, "%s() SAA_ERR_EMPTY\n", __func__);
			continue;
		}
		if (ret != SAA_OK) {
			printk(KERN_ERR "peek failed\n");
			return ret;
		}

		dprintk(DBGLVL_CMD, "%s() presponse_t->seqno = %d\n",
			__func__, presponse_t->seqno);

		dprintk(DBGLVL_CMD, "%s() presponse_t->flags = 0x%x\n",
			__func__, presponse_t->flags);

		dprintk(DBGLVL_CMD, "%s() presponse_t->size = %d\n",
			__func__, presponse_t->size);

		/* Check if the response was for our command */
		if (presponse_t->seqno != pcommand_t->seqno) {

			dprintk(DBGLVL_CMD,
				"wrong event: seqno = %d, expected seqno = %d, will dequeue regardless\n",
				presponse_t->seqno, pcommand_t->seqno);

			ret = saa7164_cmd_dequeue(dev);
			if (ret != SAA_OK) {
				printk(KERN_ERR "dequeue failed, ret = %d\n",
					ret);
				if (safety++ > 16) {
					printk(KERN_ERR
					"dequeue exceeded, safety exit\n");
					return SAA_ERR_BUSY;
				}
			}

			continue;
		}

		if ((presponse_t->flags & PVC_RESPONSEFLAG_ERROR) != 0) {

			memset(&errdata[0], 0, sizeof(errdata));

			ret = saa7164_bus_get(dev, presponse_t, &errdata[0], 0);
			if (ret != SAA_OK) {
				printk(KERN_ERR "get error(2)\n");
				return ret;
			}

			saa7164_cmd_free_seqno(dev, pcommand_t->seqno);

			dprintk(DBGLVL_CMD, "%s() errdata %02x%02x%02x%02x\n",
				__func__, errdata[0], errdata[1], errdata[2],
				errdata[3]);

			/* Map error codes */
			dprintk(DBGLVL_CMD, "%s() cmd, error code  = 0x%x\n",
				__func__, errdata[0]);

			switch (errdata[0]) {
			case PVC_ERRORCODE_INVALID_COMMAND:
				dprintk(DBGLVL_CMD, "%s() INVALID_COMMAND\n",
					__func__);
				ret = SAA_ERR_INVALID_COMMAND;
				break;
			case PVC_ERRORCODE_INVALID_DATA:
				dprintk(DBGLVL_CMD, "%s() INVALID_DATA\n",
					__func__);
				ret = SAA_ERR_BAD_PARAMETER;
				break;
			case PVC_ERRORCODE_TIMEOUT:
				dprintk(DBGLVL_CMD, "%s() TIMEOUT\n", __func__);
				ret = SAA_ERR_TIMEOUT;
				break;
			case PVC_ERRORCODE_NAK:
				dprintk(DBGLVL_CMD, "%s() NAK\n", __func__);
				ret = SAA_ERR_NULL_PACKET;
				break;
			case PVC_ERRORCODE_UNKNOWN:
			case PVC_ERRORCODE_INVALID_CONTROL:
				dprintk(DBGLVL_CMD,
					"%s() UNKNOWN OR INVALID CONTROL\n",
					__func__);
				ret = SAA_ERR_NOT_SUPPORTED;
				break;
			default:
				dprintk(DBGLVL_CMD, "%s() UNKNOWN\n", __func__);
				ret = SAA_ERR_NOT_SUPPORTED;
			}

			/* See of other commands are on the bus */
			if (saa7164_cmd_dequeue(dev) != SAA_OK)
				printk(KERN_ERR "dequeue(2) failed\n");

			return ret;
		}

		/* If response is invalid */
		if ((presponse_t->id != pcommand_t->id) ||
			(presponse_t->command != pcommand_t->command) ||
			(presponse_t->controlselector !=
				pcommand_t->controlselector) ||
			(((resp_dsize - data_recd) != presponse_t->size) &&
				!(presponse_t->flags & PVC_CMDFLAG_CONTINUE)) ||
			((resp_dsize - data_recd) < presponse_t->size)) {

			/* Invalid */
			dprintk(DBGLVL_CMD, "%s() Invalid\n", __func__);
			ret = saa7164_bus_get(dev, presponse_t, NULL, 0);
			if (ret != SAA_OK) {
				printk(KERN_ERR "get failed\n");
				return ret;
			}

			/* See of other commands are on the bus */
			if (saa7164_cmd_dequeue(dev) != SAA_OK)
				printk(KERN_ERR "dequeue(3) failed\n");
			continue;
		}

		/* OK, now we're actually getting out correct response */
		ret = saa7164_bus_get(dev, presponse_t, buf + data_recd, 0);
		if (ret != SAA_OK) {
			printk(KERN_ERR "get failed\n");
			return ret;
		}

		data_recd = presponse_t->size + data_recd;
		if (resp_dsize == data_recd) {
			dprintk(DBGLVL_CMD, "%s() Resp recd\n", __func__);
			break;
		}

		/* See of other commands are on the bus */
		if (saa7164_cmd_dequeue(dev) != SAA_OK)
			printk(KERN_ERR "dequeue(3) failed\n");
	} /* (loop) */

	/* Release the sequence number allocation */
	saa7164_cmd_free_seqno(dev, pcommand_t->seqno);

	/* if powerdown signal all pending commands */

	dprintk(DBGLVL_CMD, "%s() Calling dequeue then exit\n", __func__);

	/* See of other commands are on the bus */
	if (saa7164_cmd_dequeue(dev) != SAA_OK)
		printk(KERN_ERR "dequeue(4) failed\n");

	ret = SAA_OK;
out:
	return ret;
}


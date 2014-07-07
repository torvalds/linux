/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <subdev/pwr.h>
#include <subdev/timer.h>

static int
nouveau_pwr_send(struct nouveau_pwr *ppwr, u32 reply[2],
		 u32 process, u32 message, u32 data0, u32 data1)
{
	struct nouveau_subdev *subdev = nv_subdev(ppwr);
	u32 addr;

	/* wait for a free slot in the fifo */
	addr  = nv_rd32(ppwr, 0x10a4a0);
	if (!nv_wait_ne(ppwr, 0x10a4b0, 0xffffffff, addr ^ 8))
		return -EBUSY;

	/* we currently only support a single process at a time waiting
	 * on a synchronous reply, take the PPWR mutex and tell the
	 * receive handler what we're waiting for
	 */
	if (reply) {
		mutex_lock(&subdev->mutex);
		ppwr->recv.message = message;
		ppwr->recv.process = process;
	}

	/* acquire data segment access */
	do {
		nv_wr32(ppwr, 0x10a580, 0x00000001);
	} while (nv_rd32(ppwr, 0x10a580) != 0x00000001);

	/* write the packet */
	nv_wr32(ppwr, 0x10a1c0, 0x01000000 | (((addr & 0x07) << 4) +
				ppwr->send.base));
	nv_wr32(ppwr, 0x10a1c4, process);
	nv_wr32(ppwr, 0x10a1c4, message);
	nv_wr32(ppwr, 0x10a1c4, data0);
	nv_wr32(ppwr, 0x10a1c4, data1);
	nv_wr32(ppwr, 0x10a4a0, (addr + 1) & 0x0f);

	/* release data segment access */
	nv_wr32(ppwr, 0x10a580, 0x00000000);

	/* wait for reply, if requested */
	if (reply) {
		wait_event(ppwr->recv.wait, (ppwr->recv.process == 0));
		reply[0] = ppwr->recv.data[0];
		reply[1] = ppwr->recv.data[1];
		mutex_unlock(&subdev->mutex);
	}

	return 0;
}

static void
nouveau_pwr_recv(struct work_struct *work)
{
	struct nouveau_pwr *ppwr =
		container_of(work, struct nouveau_pwr, recv.work);
	u32 process, message, data0, data1;

	/* nothing to do if GET == PUT */
	u32 addr =  nv_rd32(ppwr, 0x10a4cc);
	if (addr == nv_rd32(ppwr, 0x10a4c8))
		return;

	/* acquire data segment access */
	do {
		nv_wr32(ppwr, 0x10a580, 0x00000002);
	} while (nv_rd32(ppwr, 0x10a580) != 0x00000002);

	/* read the packet */
	nv_wr32(ppwr, 0x10a1c0, 0x02000000 | (((addr & 0x07) << 4) +
				ppwr->recv.base));
	process = nv_rd32(ppwr, 0x10a1c4);
	message = nv_rd32(ppwr, 0x10a1c4);
	data0   = nv_rd32(ppwr, 0x10a1c4);
	data1   = nv_rd32(ppwr, 0x10a1c4);
	nv_wr32(ppwr, 0x10a4cc, (addr + 1) & 0x0f);

	/* release data segment access */
	nv_wr32(ppwr, 0x10a580, 0x00000000);

	/* wake process if it's waiting on a synchronous reply */
	if (ppwr->recv.process) {
		if (process == ppwr->recv.process &&
		    message == ppwr->recv.message) {
			ppwr->recv.data[0] = data0;
			ppwr->recv.data[1] = data1;
			ppwr->recv.process = 0;
			wake_up(&ppwr->recv.wait);
			return;
		}
	}

	/* right now there's no other expected responses from the engine,
	 * so assume that any unexpected message is an error.
	 */
	nv_warn(ppwr, "%c%c%c%c 0x%08x 0x%08x 0x%08x 0x%08x\n",
		(char)((process & 0x000000ff) >>  0),
		(char)((process & 0x0000ff00) >>  8),
		(char)((process & 0x00ff0000) >> 16),
		(char)((process & 0xff000000) >> 24),
		process, message, data0, data1);
}

static void
nouveau_pwr_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_pwr *ppwr = (void *)subdev;
	u32 disp = nv_rd32(ppwr, 0x10a01c);
	u32 intr = nv_rd32(ppwr, 0x10a008) & disp & ~(disp >> 16);

	if (intr & 0x00000020) {
		u32 stat = nv_rd32(ppwr, 0x10a16c);
		if (stat & 0x80000000) {
			nv_error(ppwr, "UAS fault at 0x%06x addr 0x%08x\n",
				 stat & 0x00ffffff, nv_rd32(ppwr, 0x10a168));
			nv_wr32(ppwr, 0x10a16c, 0x00000000);
			intr &= ~0x00000020;
		}
	}

	if (intr & 0x00000040) {
		schedule_work(&ppwr->recv.work);
		nv_wr32(ppwr, 0x10a004, 0x00000040);
		intr &= ~0x00000040;
	}

	if (intr & 0x00000080) {
		nv_info(ppwr, "wr32 0x%06x 0x%08x\n", nv_rd32(ppwr, 0x10a7a0),
						      nv_rd32(ppwr, 0x10a7a4));
		nv_wr32(ppwr, 0x10a004, 0x00000080);
		intr &= ~0x00000080;
	}

	if (intr) {
		nv_error(ppwr, "intr 0x%08x\n", intr);
		nv_wr32(ppwr, 0x10a004, intr);
	}
}

int
_nouveau_pwr_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_pwr *ppwr = (void *)object;

	nv_wr32(ppwr, 0x10a014, 0x00000060);
	flush_work(&ppwr->recv.work);

	return nouveau_subdev_fini(&ppwr->base, suspend);
}

int
_nouveau_pwr_init(struct nouveau_object *object)
{
	struct nouveau_pwr *ppwr = (void *)object;
	int ret, i;

	ret = nouveau_subdev_init(&ppwr->base);
	if (ret)
		return ret;

	nv_subdev(ppwr)->intr = nouveau_pwr_intr;
	ppwr->message = nouveau_pwr_send;

	/* prevent previous ucode from running, wait for idle, reset */
	nv_wr32(ppwr, 0x10a014, 0x0000ffff); /* INTR_EN_CLR = ALL */
	nv_wait(ppwr, 0x10a04c, 0xffffffff, 0x00000000);
	nv_mask(ppwr, 0x000200, 0x00002000, 0x00000000);
	nv_mask(ppwr, 0x000200, 0x00002000, 0x00002000);

	/* upload data segment */
	nv_wr32(ppwr, 0x10a1c0, 0x01000000);
	for (i = 0; i < ppwr->data.size / 4; i++)
		nv_wr32(ppwr, 0x10a1c4, ppwr->data.data[i]);

	/* upload code segment */
	nv_wr32(ppwr, 0x10a180, 0x01000000);
	for (i = 0; i < ppwr->code.size / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(ppwr, 0x10a188, i >> 6);
		nv_wr32(ppwr, 0x10a184, ppwr->code.data[i]);
	}

	/* start it running */
	nv_wr32(ppwr, 0x10a10c, 0x00000000);
	nv_wr32(ppwr, 0x10a104, 0x00000000);
	nv_wr32(ppwr, 0x10a100, 0x00000002);

	/* wait for valid host->pwr ring configuration */
	if (!nv_wait_ne(ppwr, 0x10a4d0, 0xffffffff, 0x00000000))
		return -EBUSY;
	ppwr->send.base = nv_rd32(ppwr, 0x10a4d0) & 0x0000ffff;
	ppwr->send.size = nv_rd32(ppwr, 0x10a4d0) >> 16;

	/* wait for valid pwr->host ring configuration */
	if (!nv_wait_ne(ppwr, 0x10a4dc, 0xffffffff, 0x00000000))
		return -EBUSY;
	ppwr->recv.base = nv_rd32(ppwr, 0x10a4dc) & 0x0000ffff;
	ppwr->recv.size = nv_rd32(ppwr, 0x10a4dc) >> 16;

	nv_wr32(ppwr, 0x10a010, 0x000000e0);
	return 0;
}

int
nouveau_pwr_create_(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, int length, void **pobject)
{
	struct nouveau_pwr *ppwr;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "PPWR",
				     "pwr", length, pobject);
	ppwr = *pobject;
	if (ret)
		return ret;

	INIT_WORK(&ppwr->recv.work, nouveau_pwr_recv);
	init_waitqueue_head(&ppwr->recv.wait);
	return 0;
}

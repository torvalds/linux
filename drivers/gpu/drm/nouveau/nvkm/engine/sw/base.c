/*
 * Copyright 2015 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "priv.h"
#include "chan.h"

#include <engine/fifo.h>

bool
nvkm_sw_mthd(struct nvkm_sw *sw, int chid, int subc, u32 mthd, u32 data)
{
	struct nvkm_sw_chan *chan;
	bool handled = false;
	unsigned long flags;

	spin_lock_irqsave(&sw->engine.lock, flags);
	list_for_each_entry(chan, &sw->chan, head) {
		if (chan->fifo->chid == chid) {
			handled = nvkm_sw_chan_mthd(chan, subc, mthd, data);
			list_del(&chan->head);
			list_add(&chan->head, &sw->chan);
			break;
		}
	}
	spin_unlock_irqrestore(&sw->engine.lock, flags);
	return handled;
}

int
nvkm_sw_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_sw *sw;
	int ret;

	ret = nvkm_engine_create_(parent, engine, oclass, true, "sw",
				  "sw", length, pobject);
	sw = *pobject;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&sw->chan);
	return 0;
}

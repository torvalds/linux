/*
 *
 * dvb_ringbuffer.c: ring buffer implementation for the dvb driver
 *
 * Copyright (C) 2003 Oliver Endriss
 * Copyright (C) 2004 Andrew de Quincey
 *
 * based on code originally found in av7110.c & dvb_ci.c:
 * Copyright (C) 1999-2003 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */



#define __KERNEL_SYSCALLS__
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include "dvb_ringbuffer.h"

#define PKT_READY 0
#define PKT_DISPOSED 1


void dvb_ringbuffer_init(struct dvb_ringbuffer *rbuf, void *data, size_t len)
{
        rbuf->pread=rbuf->pwrite=0;
        rbuf->data=data;
        rbuf->size=len;

        init_waitqueue_head(&rbuf->queue);

        spin_lock_init(&(rbuf->lock));
}



int dvb_ringbuffer_empty(struct dvb_ringbuffer *rbuf)
{
        return (rbuf->pread==rbuf->pwrite);
}



ssize_t dvb_ringbuffer_free(struct dvb_ringbuffer *rbuf)
{
        ssize_t free;

        free = rbuf->pread - rbuf->pwrite;
        if (free <= 0)
                free += rbuf->size;
        return free-1;
}



ssize_t dvb_ringbuffer_avail(struct dvb_ringbuffer *rbuf)
{
        ssize_t avail;

        avail = rbuf->pwrite - rbuf->pread;
        if (avail < 0)
                avail += rbuf->size;
        return avail;
}



void dvb_ringbuffer_flush(struct dvb_ringbuffer *rbuf)
{
        rbuf->pread = rbuf->pwrite;
}



void dvb_ringbuffer_flush_spinlock_wakeup(struct dvb_ringbuffer *rbuf)
{
        unsigned long flags;

        spin_lock_irqsave(&rbuf->lock, flags);
        dvb_ringbuffer_flush(rbuf);
        spin_unlock_irqrestore(&rbuf->lock, flags);

        wake_up(&rbuf->queue);
}



ssize_t dvb_ringbuffer_read(struct dvb_ringbuffer *rbuf, u8 *buf, size_t len, int usermem)
{
        size_t todo = len;
        size_t split;

        split = (rbuf->pread + len > rbuf->size) ? rbuf->size - rbuf->pread : 0;
        if (split > 0) {
                if (!usermem)
                        memcpy(buf, rbuf->data+rbuf->pread, split);
                else
                        if (copy_to_user(buf, rbuf->data+rbuf->pread, split))
                                return -EFAULT;
                buf += split;
                todo -= split;
                rbuf->pread = 0;
        }
        if (!usermem)
                memcpy(buf, rbuf->data+rbuf->pread, todo);
        else
                if (copy_to_user(buf, rbuf->data+rbuf->pread, todo))
                        return -EFAULT;

        rbuf->pread = (rbuf->pread + todo) % rbuf->size;

        return len;
}



ssize_t dvb_ringbuffer_write(struct dvb_ringbuffer *rbuf, const u8 *buf, size_t len)
{
        size_t todo = len;
        size_t split;

        split = (rbuf->pwrite + len > rbuf->size) ? rbuf->size - rbuf->pwrite : 0;

        if (split > 0) {
                memcpy(rbuf->data+rbuf->pwrite, buf, split);
                buf += split;
                todo -= split;
                rbuf->pwrite = 0;
        }
        memcpy(rbuf->data+rbuf->pwrite, buf, todo);
        rbuf->pwrite = (rbuf->pwrite + todo) % rbuf->size;

        return len;
}

ssize_t dvb_ringbuffer_pkt_write(struct dvb_ringbuffer *rbuf, u8* buf, size_t len)
{
        int status;
        ssize_t oldpwrite = rbuf->pwrite;

        DVB_RINGBUFFER_WRITE_BYTE(rbuf, len >> 8);
        DVB_RINGBUFFER_WRITE_BYTE(rbuf, len & 0xff);
        DVB_RINGBUFFER_WRITE_BYTE(rbuf, PKT_READY);
        status = dvb_ringbuffer_write(rbuf, buf, len);

        if (status < 0) rbuf->pwrite = oldpwrite;
        return status;
}

ssize_t dvb_ringbuffer_pkt_read(struct dvb_ringbuffer *rbuf, size_t idx,
                                int offset, u8* buf, size_t len, int usermem)
{
        size_t todo;
        size_t split;
        size_t pktlen;

        pktlen = rbuf->data[idx] << 8;
        pktlen |= rbuf->data[(idx + 1) % rbuf->size];
        if (offset > pktlen) return -EINVAL;
        if ((offset + len) > pktlen) len = pktlen - offset;

        idx = (idx + DVB_RINGBUFFER_PKTHDRSIZE + offset) % rbuf->size;
        todo = len;
        split = ((idx + len) > rbuf->size) ? rbuf->size - idx : 0;
        if (split > 0) {
                if (!usermem)
                        memcpy(buf, rbuf->data+idx, split);
                else
                        if (copy_to_user(buf, rbuf->data+idx, split))
                                return -EFAULT;
                buf += split;
                todo -= split;
                idx = 0;
        }
        if (!usermem)
                memcpy(buf, rbuf->data+idx, todo);
        else
                if (copy_to_user(buf, rbuf->data+idx, todo))
                        return -EFAULT;

        return len;
}

void dvb_ringbuffer_pkt_dispose(struct dvb_ringbuffer *rbuf, size_t idx)
{
        size_t pktlen;

        rbuf->data[(idx + 2) % rbuf->size] = PKT_DISPOSED;

        // clean up disposed packets
        while(dvb_ringbuffer_avail(rbuf) > DVB_RINGBUFFER_PKTHDRSIZE) {
                if (DVB_RINGBUFFER_PEEK(rbuf, 2) == PKT_DISPOSED) {
                        pktlen = DVB_RINGBUFFER_PEEK(rbuf, 0) << 8;
                        pktlen |= DVB_RINGBUFFER_PEEK(rbuf, 1);
                        DVB_RINGBUFFER_SKIP(rbuf, pktlen + DVB_RINGBUFFER_PKTHDRSIZE);
                } else {
                        // first packet is not disposed, so we stop cleaning now
                        break;
                }
        }
}

ssize_t dvb_ringbuffer_pkt_next(struct dvb_ringbuffer *rbuf, size_t idx, size_t* pktlen)
{
        int consumed;
        int curpktlen;
        int curpktstatus;

        if (idx == -1) {
	       idx = rbuf->pread;
	} else {
                curpktlen = rbuf->data[idx] << 8;
                curpktlen |= rbuf->data[(idx + 1) % rbuf->size];
	        idx = (idx + curpktlen + DVB_RINGBUFFER_PKTHDRSIZE) % rbuf->size;
	}

        consumed = (idx - rbuf->pread) % rbuf->size;

        while((dvb_ringbuffer_avail(rbuf) - consumed) > DVB_RINGBUFFER_PKTHDRSIZE) {

                curpktlen = rbuf->data[idx] << 8;
                curpktlen |= rbuf->data[(idx + 1) % rbuf->size];
                curpktstatus = rbuf->data[(idx + 2) % rbuf->size];

                if (curpktstatus == PKT_READY) {
                        *pktlen = curpktlen;
                        return idx;
                }

                consumed += curpktlen + DVB_RINGBUFFER_PKTHDRSIZE;
                idx = (idx + curpktlen + DVB_RINGBUFFER_PKTHDRSIZE) % rbuf->size;
        }

        // no packets available
        return -1;
}



EXPORT_SYMBOL(dvb_ringbuffer_init);
EXPORT_SYMBOL(dvb_ringbuffer_empty);
EXPORT_SYMBOL(dvb_ringbuffer_free);
EXPORT_SYMBOL(dvb_ringbuffer_avail);
EXPORT_SYMBOL(dvb_ringbuffer_flush);
EXPORT_SYMBOL(dvb_ringbuffer_flush_spinlock_wakeup);
EXPORT_SYMBOL(dvb_ringbuffer_read);
EXPORT_SYMBOL(dvb_ringbuffer_write);
EXPORT_SYMBOL(dvb_ringbuffer_pkt_write);
EXPORT_SYMBOL(dvb_ringbuffer_pkt_read);
EXPORT_SYMBOL(dvb_ringbuffer_pkt_dispose);
EXPORT_SYMBOL(dvb_ringbuffer_pkt_next);

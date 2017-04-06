/*
 * ddbridge.h: Digital Devices PCIe bridge driver
 *
 * Copyright (C) 2010-2011 Digital Devices GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _DDBRIDGE_H_
#define _DDBRIDGE_H_

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <asm/dma.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/ca.h>
#include <linux/socket.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_ringbuffer.h"
#include "dvb_ca_en50221.h"
#include "dvb_net.h"
#include "cxd2099.h"

#define DDB_MAX_I2C     4
#define DDB_MAX_PORT    4
#define DDB_MAX_INPUT   8
#define DDB_MAX_OUTPUT  4

struct ddb_info {
	int   type;
#define DDB_NONE         0
#define DDB_OCTOPUS      1
	char *name;
	int   port_num;
	u32   port_type[DDB_MAX_PORT];
};

/* DMA_SIZE MUST be divisible by 188 and 128 !!! */

#define INPUT_DMA_MAX_BUFS 32      /* hardware table limit */
#define INPUT_DMA_BUFS 8
#define INPUT_DMA_SIZE (128*47*21)

#define OUTPUT_DMA_MAX_BUFS 32
#define OUTPUT_DMA_BUFS 8
#define OUTPUT_DMA_SIZE (128*47*21)

struct ddb;
struct ddb_port;

struct ddb_input {
	struct ddb_port       *port;
	u32                    nr;
	int                    attached;

	dma_addr_t             pbuf[INPUT_DMA_MAX_BUFS];
	u8                    *vbuf[INPUT_DMA_MAX_BUFS];
	u32                    dma_buf_num;
	u32                    dma_buf_size;

	struct tasklet_struct  tasklet;
	spinlock_t             lock;
	wait_queue_head_t      wq;
	int                    running;
	u32                    stat;
	u32                    cbuf;
	u32                    coff;

	struct dvb_adapter     adap;
	struct dvb_device     *dev;
	struct dvb_frontend   *fe;
	struct dvb_frontend   *fe2;
	struct dmxdev          dmxdev;
	struct dvb_demux       demux;
	struct dvb_net         dvbnet;
	struct dmx_frontend    hw_frontend;
	struct dmx_frontend    mem_frontend;
	int                    users;
	int (*gate_ctrl)(struct dvb_frontend *, int);
};

struct ddb_output {
	struct ddb_port       *port;
	u32                    nr;
	dma_addr_t             pbuf[OUTPUT_DMA_MAX_BUFS];
	u8                    *vbuf[OUTPUT_DMA_MAX_BUFS];
	u32                    dma_buf_num;
	u32                    dma_buf_size;
	struct tasklet_struct  tasklet;
	spinlock_t             lock;
	wait_queue_head_t      wq;
	int                    running;
	u32                    stat;
	u32                    cbuf;
	u32                    coff;

	struct dvb_adapter     adap;
	struct dvb_device     *dev;
};

struct ddb_i2c {
	struct ddb            *dev;
	u32                    nr;
	struct i2c_adapter     adap;
	struct i2c_adapter     adap2;
	u32                    regs;
	u32                    rbuf;
	u32                    wbuf;
	int                    done;
	wait_queue_head_t      wq;
};

struct ddb_port {
	struct ddb            *dev;
	u32                    nr;
	struct ddb_i2c        *i2c;
	struct mutex           i2c_gate_lock;
	u32                    class;
#define DDB_PORT_NONE           0
#define DDB_PORT_CI             1
#define DDB_PORT_TUNER          2
	u32                    type;
#define DDB_TUNER_NONE          0
#define DDB_TUNER_DVBS_ST       1
#define DDB_TUNER_DVBS_ST_AA    2
#define DDB_TUNER_DVBCT_TR     16
#define DDB_TUNER_DVBCT_ST     17
	u32                    adr;

	struct ddb_input      *input[2];
	struct ddb_output     *output;
	struct dvb_ca_en50221 *en;
};

struct ddb {
	struct pci_dev        *pdev;
	unsigned char __iomem *regs;
	struct ddb_port        port[DDB_MAX_PORT];
	struct ddb_i2c         i2c[DDB_MAX_I2C];
	struct ddb_input       input[DDB_MAX_INPUT];
	struct ddb_output      output[DDB_MAX_OUTPUT];

	struct device         *ddb_dev;
	int                    nr;
	u8                     iobuf[1028];

	struct ddb_info       *info;
	int                    msi;
};

/****************************************************************************/

#define ddbwritel(_val, _adr)        writel((_val), \
				     dev->regs+(_adr))
#define ddbreadl(_adr)               readl(dev->regs+(_adr))
#define ddbcpyto(_adr, _src, _count) memcpy_toio(dev->regs+(_adr), (_src), (_count))
#define ddbcpyfrom(_dst, _adr, _count) memcpy_fromio((_dst), dev->regs+(_adr), (_count))

/****************************************************************************/

#endif

/*
 * ddbridge.h: Digital Devices PCIe bridge driver
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rmetzler@digitaldevices.de>
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/swab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/completion.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <linux/dvb/ca.h>
#include <linux/socket.h>
#include <linux/device.h>
#include <linux/io.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_ringbuffer.h>
#include <media/dvb_ca_en50221.h>
#include <media/dvb_net.h>

#define DDBRIDGE_VERSION "0.9.33-integrated"

#define DDB_MAX_I2C    32
#define DDB_MAX_PORT   32
#define DDB_MAX_INPUT  64
#define DDB_MAX_OUTPUT 32
#define DDB_MAX_LINK    4
#define DDB_LINK_SHIFT 28

#define DDB_LINK_TAG(_x) (_x << DDB_LINK_SHIFT)

struct ddb_regset {
	u32 base;
	u32 num;
	u32 size;
};

struct ddb_regmap {
	u32 irq_base_i2c;
	u32 irq_base_idma;
	u32 irq_base_odma;

	const struct ddb_regset *i2c;
	const struct ddb_regset *i2c_buf;
	const struct ddb_regset *idma;
	const struct ddb_regset *idma_buf;
	const struct ddb_regset *odma;
	const struct ddb_regset *odma_buf;

	const struct ddb_regset *input;
	const struct ddb_regset *output;

	const struct ddb_regset *channel;
};

struct ddb_ids {
	u16 vendor;
	u16 device;
	u16 subvendor;
	u16 subdevice;

	u32 hwid;
	u32 regmapid;
	u32 devid;
	u32 mac;
};

struct ddb_info {
	int   type;
#define DDB_NONE            0
#define DDB_OCTOPUS         1
#define DDB_OCTOPUS_CI      2
#define DDB_OCTOPUS_MAX     5
#define DDB_OCTOPUS_MAX_CT  6
#define DDB_OCTOPUS_MCI     9
	char *name;
	u32   i2c_mask;
	u8    port_num;
	u8    led_num;
	u8    fan_num;
	u8    temp_num;
	u8    temp_bus;
	u32   board_control;
	u32   board_control_2;
	u8    mdio_num;
	u8    con_clock; /* use a continuous clock */
	u8    ts_quirks;
#define TS_QUIRK_SERIAL   1
#define TS_QUIRK_REVERSED 2
#define TS_QUIRK_ALT_OSC  8
	u32   tempmon_irq;
	u8    mci;
	const struct ddb_regmap *regmap;
};

#define DMA_MAX_BUFS 32      /* hardware table limit */

struct ddb;
struct ddb_port;

struct ddb_dma {
	void                  *io;
	u32                    regs;
	u32                    bufregs;

	dma_addr_t             pbuf[DMA_MAX_BUFS];
	u8                    *vbuf[DMA_MAX_BUFS];
	u32                    num;
	u32                    size;
	u32                    div;
	u32                    bufval;

	struct work_struct     work;
	spinlock_t             lock; /* DMA lock */
	wait_queue_head_t      wq;
	int                    running;
	u32                    stat;
	u32                    ctrl;
	u32                    cbuf;
	u32                    coff;
};

struct ddb_dvb {
	struct dvb_adapter    *adap;
	int                    adap_registered;
	struct dvb_device     *dev;
	struct i2c_client     *i2c_client[1];
	struct dvb_frontend   *fe;
	struct dvb_frontend   *fe2;
	struct dmxdev          dmxdev;
	struct dvb_demux       demux;
	struct dvb_net         dvbnet;
	struct dmx_frontend    hw_frontend;
	struct dmx_frontend    mem_frontend;
	int                    users;
	u32                    attached;
	u8                     input;

	enum fe_sec_tone_mode  tone;
	enum fe_sec_voltage    voltage;

	int (*i2c_gate_ctrl)(struct dvb_frontend *, int);
	int (*set_voltage)(struct dvb_frontend *fe,
			   enum fe_sec_voltage voltage);
	int (*set_input)(struct dvb_frontend *fe, int input);
	int (*diseqc_send_master_cmd)(struct dvb_frontend *fe,
				      struct dvb_diseqc_master_cmd *cmd);
};

struct ddb_ci {
	struct dvb_ca_en50221  en;
	struct ddb_port       *port;
	u32                    nr;
};

struct ddb_io {
	struct ddb_port       *port;
	u32                    nr;
	u32                    regs;
	struct ddb_dma        *dma;
	struct ddb_io         *redo;
	struct ddb_io         *redi;
};

#define ddb_output ddb_io
#define ddb_input ddb_io

struct ddb_i2c {
	struct ddb            *dev;
	u32                    nr;
	u32                    regs;
	u32                    link;
	struct i2c_adapter     adap;
	u32                    rbuf;
	u32                    wbuf;
	u32                    bsize;
	struct completion      completion;
};

struct ddb_port {
	struct ddb            *dev;
	u32                    nr;
	u32                    pnr;
	u32                    regs;
	u32                    lnr;
	struct ddb_i2c        *i2c;
	struct mutex           i2c_gate_lock; /* I2C access lock */
	u32                    class;
#define DDB_PORT_NONE           0
#define DDB_PORT_CI             1
#define DDB_PORT_TUNER          2
#define DDB_PORT_LOOP           3
	char                   *name;
	char                   *type_name;
	u32                     type;
#define DDB_TUNER_DUMMY          0xffffffff
#define DDB_TUNER_NONE           0
#define DDB_TUNER_DVBS_ST        1
#define DDB_TUNER_DVBS_ST_AA     2
#define DDB_TUNER_DVBCT_TR       3
#define DDB_TUNER_DVBCT_ST       4
#define DDB_CI_INTERNAL          5
#define DDB_CI_EXTERNAL_SONY     6
#define DDB_TUNER_DVBCT2_SONY_P  7
#define DDB_TUNER_DVBC2T2_SONY_P 8
#define DDB_TUNER_ISDBT_SONY_P   9
#define DDB_TUNER_DVBS_STV0910_P 10
#define DDB_TUNER_MXL5XX         11
#define DDB_CI_EXTERNAL_XO2      12
#define DDB_CI_EXTERNAL_XO2_B    13
#define DDB_TUNER_DVBS_STV0910_PR 14
#define DDB_TUNER_DVBC2T2I_SONY_P 15
#define DDB_TUNER_MCI            16

#define DDB_TUNER_XO2            32
#define DDB_TUNER_DVBS_STV0910   (DDB_TUNER_XO2 + 0)
#define DDB_TUNER_DVBCT2_SONY    (DDB_TUNER_XO2 + 1)
#define DDB_TUNER_ISDBT_SONY     (DDB_TUNER_XO2 + 2)
#define DDB_TUNER_DVBC2T2_SONY   (DDB_TUNER_XO2 + 3)
#define DDB_TUNER_ATSC_ST        (DDB_TUNER_XO2 + 4)
#define DDB_TUNER_DVBC2T2I_SONY  (DDB_TUNER_XO2 + 5)

	struct ddb_input      *input[2];
	struct ddb_output     *output;
	struct dvb_ca_en50221 *en;
	u8                     en_freedata;
	struct ddb_dvb         dvb[2];
	u32                    gap;
	u32                    obr;
	u8                     creg;
};

#define CM_STARTUP_DELAY 2
#define CM_AVERAGE  20
#define CM_GAIN     10

#define HW_LSB_SHIFT    12
#define HW_LSB_MASK     0x1000

#define CM_IDLE    0
#define CM_STARTUP 1
#define CM_ADJUST  2

#define TS_CAPTURE_LEN  (4096)

struct ddb_lnb {
	struct mutex           lock; /* lock lnb access */
	u32                    tone;
	enum fe_sec_voltage    oldvoltage[4];
	u32                    voltage[4];
	u32                    voltages;
	u32                    fmode;
};

struct ddb_irq {
	void                   (*handler)(void *);
	void                  *data;
};

struct ddb_link {
	struct ddb            *dev;
	const struct ddb_info *info;
	u32                    nr;
	u32                    regs;
	spinlock_t             lock; /* lock link access */
	struct mutex           flash_mutex; /* lock flash access */
	struct ddb_lnb         lnb;
	struct tasklet_struct  tasklet;
	struct ddb_ids         ids;

	spinlock_t             temp_lock; /* lock temp chip access */
	int                    overtemperature_error;
	u8                     temp_tab[11];
	struct ddb_irq         irq[256];
};

struct ddb {
	struct pci_dev          *pdev;
	struct platform_device  *pfdev;
	struct device           *dev;

	int                      msi;
	struct workqueue_struct *wq;
	u32                      has_dma;

	struct ddb_link          link[DDB_MAX_LINK];
	unsigned char __iomem   *regs;
	u32                      regs_len;
	u32                      port_num;
	struct ddb_port          port[DDB_MAX_PORT];
	u32                      i2c_num;
	struct ddb_i2c           i2c[DDB_MAX_I2C];
	struct ddb_input         input[DDB_MAX_INPUT];
	struct ddb_output        output[DDB_MAX_OUTPUT];
	struct dvb_adapter       adap[DDB_MAX_INPUT];
	struct ddb_dma           idma[DDB_MAX_INPUT];
	struct ddb_dma           odma[DDB_MAX_OUTPUT];

	struct device           *ddb_dev;
	u32                      ddb_dev_users;
	u32                      nr;
	u8                       iobuf[1028];

	u8                       leds;
	u32                      ts_irq;
	u32                      i2c_irq;

	struct mutex             mutex; /* lock access to global ddb array */

	u8                       tsbuf[TS_CAPTURE_LEN];
};

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

int ddbridge_flashread(struct ddb *dev, u32 link, u8 *buf, u32 addr, u32 len);

/****************************************************************************/

/* ddbridge-core.c */
struct ddb_irq *ddb_irq_set(struct ddb *dev, u32 link, u32 nr,
			    void (*handler)(void *), void *data);
void ddb_ports_detach(struct ddb *dev);
void ddb_ports_release(struct ddb *dev);
void ddb_buffers_free(struct ddb *dev);
void ddb_device_destroy(struct ddb *dev);
irqreturn_t ddb_irq_handler0(int irq, void *dev_id);
irqreturn_t ddb_irq_handler1(int irq, void *dev_id);
irqreturn_t ddb_irq_handler(int irq, void *dev_id);
void ddb_ports_init(struct ddb *dev);
int ddb_buffers_alloc(struct ddb *dev);
int ddb_ports_attach(struct ddb *dev);
int ddb_device_create(struct ddb *dev);
int ddb_init(struct ddb *dev);
void ddb_unmap(struct ddb *dev);
int ddb_exit_ddbridge(int stage, int error);
int ddb_init_ddbridge(void);

#endif /* DDBRIDGE_H */

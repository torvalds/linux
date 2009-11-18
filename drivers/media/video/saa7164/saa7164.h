/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2009 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
	Driver architecture
	*******************

	saa7164_core.c/buffer.c/cards.c/i2c.c/dvb.c
		|	: Standard Linux driver framework for creating
		|	: exposing and managing interfaces to the rest
		|	: of the kernel or userland. Also uses _fw.c to load
		|	: firmware direct into the PCIe bus, bypassing layers.
		V
	saa7164_api..()	: Translate kernel specific functions/features
		|	: into command buffers.
		V
	saa7164_cmd..()	: Manages the flow of command packets on/off,
		|	: the bus. Deal with bus errors, timeouts etc.
		V
	saa7164_bus..() : Manage a read/write memory ring buffer in the
		|	: PCIe Address space.
		|
		|		saa7164_fw...()	: Load any frimware
		|			|	: direct into the device
		V			V
	<- ----------------- PCIe address space -------------------- ->
*/

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/kdev_t.h>

#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/videobuf-dma-sg.h>
#include <media/videobuf-dvb.h>

#include "saa7164-reg.h"
#include "saa7164-types.h"

#include <linux/version.h>
#include <linux/mutex.h>

#define SAA7164_MAXBOARDS 8

#define UNSET (-1U)
#define SAA7164_BOARD_NOAUTO			UNSET
#define SAA7164_BOARD_UNKNOWN			0
#define SAA7164_BOARD_UNKNOWN_REV2		1
#define SAA7164_BOARD_UNKNOWN_REV3		2
#define SAA7164_BOARD_HAUPPAUGE_HVR2250		3
#define SAA7164_BOARD_HAUPPAUGE_HVR2200		4
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_2	5
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_3	6
#define SAA7164_BOARD_HAUPPAUGE_HVR2250_2	7
#define SAA7164_BOARD_HAUPPAUGE_HVR2250_3	8

#define SAA7164_MAX_UNITS		8
#define SAA7164_TS_NUMBER_OF_LINES	312
#define SAA7164_PT_ENTRIES		16 /* (312 * 188) / 4096 */

#define DBGLVL_FW    4
#define DBGLVL_DVB   8
#define DBGLVL_I2C  16
#define DBGLVL_API  32
#define DBGLVL_CMD  64
#define DBGLVL_BUS 128
#define DBGLVL_IRQ 256
#define DBGLVL_BUF 512

enum port_t {
	SAA7164_MPEG_UNDEFINED = 0,
	SAA7164_MPEG_DVB,
};

enum saa7164_i2c_bus_nr {
	SAA7164_I2C_BUS_0 = 0,
	SAA7164_I2C_BUS_1,
	SAA7164_I2C_BUS_2,
};

enum saa7164_buffer_flags {
	SAA7164_BUFFER_UNDEFINED = 0,
	SAA7164_BUFFER_FREE,
	SAA7164_BUFFER_BUSY,
	SAA7164_BUFFER_FULL
};

enum saa7164_unit_type {
	SAA7164_UNIT_UNDEFINED = 0,
	SAA7164_UNIT_DIGITAL_DEMODULATOR,
	SAA7164_UNIT_ANALOG_DEMODULATOR,
	SAA7164_UNIT_TUNER,
	SAA7164_UNIT_EEPROM,
	SAA7164_UNIT_ZILOG_IRBLASTER,
	SAA7164_UNIT_ENCODER,
};

/* The PCIe bridge doesn't grant direct access to i2c.
 * Instead, you address i2c devices using a uniqely
 * allocated 'unitid' value via a messaging API. This
 * is a problem. The kernel and existing demod/tuner
 * drivers expect to talk 'i2c', so we have to maintain
 * a translation layer, and a series of functions to
 * convert i2c bus + device address into a unit id.
 */
struct saa7164_unit {
	enum saa7164_unit_type type;
	u8	id;
	char	*name;
	enum saa7164_i2c_bus_nr i2c_bus_nr;
	u8	i2c_bus_addr;
	u8	i2c_reg_len;
};

struct saa7164_board {
	char	*name;
	enum port_t porta, portb;
	enum {
		SAA7164_CHIP_UNDEFINED = 0,
		SAA7164_CHIP_REV2,
		SAA7164_CHIP_REV3,
	} chiprev;
	struct	saa7164_unit unit[SAA7164_MAX_UNITS];
};

struct saa7164_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

struct saa7164_fw_status {

	/* RISC Core details */
	u32	status;
	u32	mode;
	u32	spec;
	u32	inst;
	u32	cpuload;
	u32	remainheap;

	/* Firmware version */
	u32	version;
	u32	major;
	u32	sub;
	u32	rel;
	u32	buildnr;
};

struct saa7164_dvb {
	struct mutex lock;
	struct dvb_adapter adapter;
	struct dvb_frontend *frontend;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend fe_hw;
	struct dmx_frontend fe_mem;
	struct dvb_net net;
	int feeding;
};

struct saa7164_i2c {
	struct saa7164_dev		*dev;

	enum saa7164_i2c_bus_nr		nr;

	/* I2C I/O */
	struct i2c_adapter		i2c_adap;
	struct i2c_algo_bit_data	i2c_algo;
	struct i2c_client		i2c_client;
	u32				i2c_rc;
};

struct saa7164_tsport;

struct saa7164_buffer {
	struct list_head list;

	u32 nr;

	struct saa7164_tsport *port;

	/* Hardware Specific */
	/* PCI Memory allocations */
	enum saa7164_buffer_flags flags; /* Free, Busy, Full */

	/* A block of page align PCI memory */
	u32 pci_size;	/* PCI allocation size in bytes */
	u64 *cpu;	/* Virtual address */
	dma_addr_t dma;	/* Physical address */

	/* A page table that splits the block into a number of entries */
	u32 pt_size;		/* PCI allocation size in bytes */
	u64 *pt_cpu;		/* Virtual address */
	dma_addr_t pt_dma;	/* Physical address */
};

struct saa7164_tsport {

	struct saa7164_dev *dev;
	int nr;
	enum port_t type;

	struct saa7164_dvb dvb;

	/* HW related stream parameters */
	tmHWStreamParameters_t hw_streamingparams;

	/* DMA configuration values, is seeded during initialization */
	tmComResDMATermDescrHeader_t hwcfg;

	/* hardware specific registers */
	u32 bufcounter;
	u32 pitch;
	u32 bufsize;
	u32 bufoffset;
	u32 bufptr32l;
	u32 bufptr32h;
	u64 bufptr64;

	u32 numpte;	/* Number of entries in array, only valid in head */
	struct mutex dmaqueue_lock;
	struct mutex dummy_dmaqueue_lock;
	struct saa7164_buffer dmaqueue;
	struct saa7164_buffer dummy_dmaqueue;

};

struct saa7164_dev {
	struct list_head	devlist;
	atomic_t		refcount;

	/* pci stuff */
	struct pci_dev	*pci;
	unsigned char	pci_rev, pci_lat;
	int		pci_bus, pci_slot;
	u32		__iomem *lmmio;
	u8		__iomem *bmmio;
	u32		__iomem *lmmio2;
	u8		__iomem *bmmio2;
	int		pci_irqmask;

	/* board details */
	int	nr;
	int	hwrevision;
	u32	board;
	char	name[32];

	/* firmware status */
	struct saa7164_fw_status	fw_status;

	tmComResHWDescr_t		hwdesc;
	tmComResInterfaceDescr_t	intfdesc;
	tmComResBusDescr_t		busdesc;

	tmComResBusInfo_t		bus;

	/* Interrupt status and ack registers */
	u32 int_status;
	u32 int_ack;

	struct cmd			cmds[SAA_CMD_MAX_MSG_UNITS];
	struct mutex			lock;

	/* I2c related */
	struct saa7164_i2c i2c_bus[3];

	/* Transport related */
	struct saa7164_tsport ts1, ts2;

	/* Deferred command/api interrupts handling */
	struct work_struct workcmd;

};

extern struct list_head saa7164_devlist;
extern unsigned int waitsecs;

/* ----------------------------------------------------------- */
/* saa7164-core.c                                              */
void saa7164_dumpregs(struct saa7164_dev *dev, u32 addr);
void saa7164_dumphex16(struct saa7164_dev *dev, u8 *buf, int len);
void saa7164_getfirmwarestatus(struct saa7164_dev *dev);
u32 saa7164_getcurrentfirmwareversion(struct saa7164_dev *dev);

/* ----------------------------------------------------------- */
/* saa7164-fw.c                                                */
int saa7164_downloadfirmware(struct saa7164_dev *dev);

/* ----------------------------------------------------------- */
/* saa7164-i2c.c                                               */
extern int saa7164_i2c_register(struct saa7164_i2c *bus);
extern int saa7164_i2c_unregister(struct saa7164_i2c *bus);
extern void saa7164_call_i2c_clients(struct saa7164_i2c *bus,
	unsigned int cmd, void *arg);

/* ----------------------------------------------------------- */
/* saa7164-bus.c                                               */
int saa7164_bus_setup(struct saa7164_dev *dev);
void saa7164_bus_dump(struct saa7164_dev *dev);
int saa7164_bus_set(struct saa7164_dev *dev, tmComResInfo_t* msg, void *buf);
int saa7164_bus_get(struct saa7164_dev *dev, tmComResInfo_t* msg,
	void *buf, int peekonly);

/* ----------------------------------------------------------- */
/* saa7164-cmd.c                                               */
int saa7164_cmd_send(struct saa7164_dev *dev,
	u8 id, tmComResCmd_t command, u16 controlselector,
	u16 size, void *buf);
void saa7164_cmd_signal(struct saa7164_dev *dev, u8 seqno);
int saa7164_irq_dequeue(struct saa7164_dev *dev);

/* ----------------------------------------------------------- */
/* saa7164-api.c                                               */
int saa7164_api_get_fw_version(struct saa7164_dev *dev, u32 *version);
int saa7164_api_enum_subdevs(struct saa7164_dev *dev);
int saa7164_api_i2c_read(struct saa7164_i2c *bus, u8 addr, u32 reglen, u8 *reg,
	u32 datalen, u8 *data);
int saa7164_api_i2c_write(struct saa7164_i2c *bus, u8 addr,
	u32 datalen, u8 *data);
int saa7164_api_dif_write(struct saa7164_i2c *bus, u8 addr,
	u32 datalen, u8 *data);
int saa7164_api_read_eeprom(struct saa7164_dev *dev, u8 *buf, int buflen);
int saa7164_api_set_gpiobit(struct saa7164_dev *dev, u8 unitid, u8 pin);
int saa7164_api_clear_gpiobit(struct saa7164_dev *dev, u8 unitid, u8 pin);
int saa7164_api_transition_port(struct saa7164_tsport *port, u8 mode);

/* ----------------------------------------------------------- */
/* saa7164-cards.c                                             */
extern struct saa7164_board saa7164_boards[];
extern const unsigned int saa7164_bcount;

extern struct saa7164_subid saa7164_subids[];
extern const unsigned int saa7164_idcount;

extern void saa7164_card_list(struct saa7164_dev *dev);
extern void saa7164_gpio_setup(struct saa7164_dev *dev);
extern void saa7164_card_setup(struct saa7164_dev *dev);

extern int saa7164_i2caddr_to_reglen(struct saa7164_i2c *bus, int addr);
extern int saa7164_i2caddr_to_unitid(struct saa7164_i2c *bus, int addr);
extern char *saa7164_unitid_name(struct saa7164_dev *dev, u8 unitid);

/* ----------------------------------------------------------- */
/* saa7164-dvb.c                                               */
extern int saa7164_dvb_register(struct saa7164_tsport *port);
extern int saa7164_dvb_unregister(struct saa7164_tsport *port);

/* ----------------------------------------------------------- */
/* saa7164-buffer.c                                            */
extern struct saa7164_buffer *saa7164_buffer_alloc(struct saa7164_tsport *port,
	u32 len);
extern int saa7164_buffer_dealloc(struct saa7164_tsport *port,
	struct saa7164_buffer *buf);

/* ----------------------------------------------------------- */

extern unsigned int saa_debug;
#define dprintk(level, fmt, arg...)\
	do { if (saa_debug & level)\
		printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
	} while (0)

#define log_warn(fmt, arg...)\
	do { \
		printk(KERN_WARNING "%s: " fmt, dev->name, ## arg);\
	} while (0)

#define log_err(fmt, arg...)\
	do { \
		printk(KERN_ERROR "%s: " fmt, dev->name, ## arg);\
	} while (0)

#define saa7164_readl(reg) readl(dev->lmmio + ((reg) >> 2))
#define saa7164_writel(reg, value) writel((value), dev->lmmio + ((reg) >> 2))


#define saa7164_readb(reg)             readl(dev->bmmio + (reg))
#define saa7164_writeb(reg, value)     writel((value), dev->bmmio + (reg))

